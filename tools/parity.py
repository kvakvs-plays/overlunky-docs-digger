"""Parity report: the digger's JSON against overlunky's old regex parser (docs/parse_source.py) as the oracle.

Usage: python tools/parity.py <overlunky root> <docs-digger JSON>

Checks every global function the old parser reports (ps.funcs, ps.deprecated_funcs):
  presence, deprecated flag, parameter counts and documentation text,
and lists every type the digger left unclassified. Differences that are understood are listed in ACCEPTED with
their reason; anything else makes the script exit with 1.
"""
import json
import os
import re
import sys

# Understood differences, by Lua name: why the old and the new result differ and why the new one is right.
ACCEPTED = {
    'create_fmod_guid_map': 'the old parser keeps `///` lines across code lines, so it also takes the comment of the '
                            'FMODguidMap usertype registered above; the digger keeps only the block above the binding',
}


def load_old_parser(overlunky_root):
    """Runs the old parser in overlunky's docs/ directory and returns the module with its results."""
    docs_dir = os.path.join(overlunky_root, 'docs')
    os.chdir(docs_dir)
    sys.path.insert(0, docs_dir)
    import parse_source as ps  # noqa: E402 (the old parser only works from its own directory)

    ps.ENABLE_CACHING = False
    ps.configure_parse(lambda text: text, 'docs-digger-parity.pickle')
    ps.run_parse()
    return ps


def count_params(ps, param_text):
    """Number of comma-separated parameters, as the old generator splits them."""
    return len([p for p in ps.custom_split(param_text) if p])


def old_param_counts(ps, func):
    """Parameter counts the old generator prints for func (one per overload), or None when it cannot see them."""
    overloads = ps.rpcfunc(func['cpp'])
    if overloads:
        return {count_params(ps, overload['param']) for overload in overloads}
    match = re.search(r'\(([^\{]*)\)', func['cpp'])
    if 'overload(' in func['cpp'] or not match:
        return None  # sol::overload or parameters on later lines: the line-based parser sees no signature
    return {count_params(ps, match.group(1))}


def doc_text(lines):
    """Comment lines compared without surrounding whitespace and blank edge lines."""
    stripped = [line.strip() for line in lines]
    while stripped and not stripped[0]:
        stripped.pop(0)
    while stripped and not stripped[-1]:
        stripped.pop()
    return stripped


def old_doc(lines):
    """The old comment without its leading NoDoc / Deprecated directive lines, as the digger stores it."""
    lines = list(lines)
    while lines and (lines[0].strip().startswith('NoDoc') or lines[0].strip() == 'Deprecated'):
        lines.pop(0)
    return doc_text(lines)


def walk_types(type_info, path, found):
    """Collects (path, spelling) of every unknown kind inside type_info."""
    if type_info['kind'] == 'unknown':
        found.append((path, type_info['spelling']))
    for inner in type_info.get('inner', []) + type_info.get('returns', []):
        walk_types(inner, path, found)


def unknown_types(digger):
    """Unclassified types in all compiled bindings (doc-only functions are text only and listed apart)."""
    found = []
    for function in digger['functions']:
        if function.get('doc_only'):
            continue
        name = '.'.join(function.get('owner', []) + [function['name']])
        for overload in function['overloads']:
            for param in overload['params']:
                walk_types(param['type'], f'{name}({param["name"]})', found)
            for returned in overload['returns']:
                walk_types(returned, f'{name} -> ', found)
    for value in digger['values']:
        if not value.get('doc_only'):
            walk_types(value['type'], '.'.join(value.get('owner', []) + [value['name']]), found)
    return found


class Report:
    """Collects the differences, split into unexplained ones and accepted ones."""

    def __init__(self):
        self.problems = []
        self.accepted = []

    def add(self, name, message):
        """Records one difference for name; accepted when ACCEPTED explains it."""
        if name in ACCEPTED:
            self.accepted.append(f'{name}: {message} (accepted: {ACCEPTED[name]})')
        else:
            self.problems.append(f'{name}: {message}')


def check_function(report, ps, func, deprecated, bindings):
    """Compares one old function with all digger bindings of the same global name."""
    name = func['name']
    if not bindings:
        report.add(name, 'missing in the digger output')
        return
    if any(b.get('deprecated', False) for b in bindings) != deprecated:
        report.add(name, f'deprecated: old {deprecated}, digger {not deprecated}')
    old_counts = old_param_counts(ps, func)
    new_counts = {len(o['params']) for b in bindings for o in b['overloads']}
    if old_counts is not None and not old_counts <= new_counts:
        report.add(name, f'param counts: old {sorted(old_counts)}, digger {sorted(new_counts)}')
    old = old_doc(func['comment'])
    if not any(doc_text(b.get('doc', [])) == old for b in bindings):
        new = doc_text(bindings[0].get('doc', []))
        report.add(name, f'doc differs:\n      old:    {old}\n      digger: {new}')


def main():
    """Runs both parsers' results through the checks and prints the report."""
    overlunky_root, digger_file = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    with open(digger_file, encoding='utf-8') as f:
        digger = json.load(f)
    ps = load_old_parser(overlunky_root)

    globals_by_name = {}
    for function in digger['functions']:
        if not function.get('owner') and 'root' not in function:
            globals_by_name.setdefault(function['name'], []).append(function)
    value_names = {v['name'] for v in digger['values'] if not v.get('owner') and 'root' not in v}

    report = Report()
    old_functions = [(f, False) for f in ps.funcs] + [(f, True) for f in ps.deprecated_funcs]
    values_in_digger = []
    for func, deprecated in old_functions:
        if func['name'] in value_names and func['name'] not in globals_by_name:
            values_in_digger.append(func['name'])  # not callable: values are checked in DIG-3 (P13)
            continue
        check_function(report, ps, func, deprecated, globals_by_name.get(func['name'], []))

    unknown = unknown_types(digger)
    doc_only = sorted(f['name'] for f in digger['functions'] + digger['values'] if f.get('doc_only'))
    print(f'old parser: {len(ps.funcs)} functions + {len(ps.deprecated_funcs)} deprecated')
    print(f'digger: {len(digger["functions"])} functions ({len(globals_by_name)} global names), '
          f'{len(digger["values"])} values')
    print(f'old functions that are values in the digger (checked in DIG-3): {sorted(values_in_digger)}')
    print(f'doc-only functions (text signatures, kinds not classified): {doc_only}')
    print(f'\nunclassified types: {len(unknown)}')
    for path, spelling in unknown:
        print(f'  {path}: {spelling}')
    print(f'\naccepted differences: {len(report.accepted)}')
    for line in report.accepted:
        print(f'  {line}')
    print(f'\nunexplained differences: {len(report.problems)}')
    for line in report.problems:
        print(f'  {line}')
    return 1 if report.problems or unknown else 0


if __name__ == '__main__':
    sys.exit(main())
