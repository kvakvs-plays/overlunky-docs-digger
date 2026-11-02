"""Parity report: the digger's JSON against overlunky's old regex parser (docs/parse_source.py) as the oracle.

Usage: python tools/parity.py <overlunky root> <docs-digger JSON>

Checks every global function the old parser reports (ps.funcs, ps.deprecated_funcs):
  presence, deprecated flag, parameter counts and documentation text;
every usertype (ps.types, ps.constructors):
  presence, bases, type comment, constructors, and per member presence, kind (function or not) and docs;
and lists every type the digger left unclassified. Differences that are understood are listed in ACCEPTED and
ACCEPTED_PATTERNS with their reason; anything else makes the script exit with 1.
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

# Understood differences for groups of names: (name regex, message prefix, reason).
ACCEPTED_PATTERNS = [
    (r'.*', 'kind: old value, digger method',
     'bound through a local sol::overload or self_return<&T::f>(): the digger resolves the callable in the AST, the old '
     'parser only sees the variable or wrapper text'),
    (r'.* constructors$', 'param counts',
     'the old parser lists every C++ constructor of the class; the digger lists the ones sol::constructors<...> '
     'exposes to Lua'),
    (r'CookFire$', 'bases',
     'CookFire is registered without sol::bases; the old regex runs on into the next registration'),
    (r'(Triangle|Quad)\.(split|is_point_inside)$', 'doc differs',
     'math.hpp documents these in a `/* // just for the autodoc ... */` block of fake declarations, which the old text '
     'parser reads and Clang does not; overlunky should move the comments onto the real declarations'),
    (r'CustomTheme\.\w+$', 'member missing',
     'level_lua.cpp binds these CustomTheme members inside a /* ... */ block comment, which the old text parser still '
     'reads; they are not bound (Lua reaches them through the ThemeInfo base)'),
]

# Usertype members the old parser generates for vtable hooks; checked with the callback passes (DIG-4).
VTABLE_MEMBERS = ('set_pre_', 'set_post_', 'clear_virtual')


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


def repair_encoding(text):
    """The old parser reads sources with the Windows code page, so UTF-8 text (`✱`) arrives as mojibake (`âœ±`)."""
    try:
        return text.encode('cp1252').decode('utf-8')
    except (UnicodeEncodeError, UnicodeDecodeError):
        return text


def old_doc(lines):
    """The old comment without its leading NoDoc / Deprecated directive lines, as the digger stores it."""
    lines = [repair_encoding(line) for line in lines]
    while lines and (lines[0].strip().startswith('NoDoc') or lines[0].strip() == 'Deprecated'):
        lines.pop(0)
    return doc_text(lines)


def walk_types(type_info, path, found):
    """Collects (path, spelling) of every unknown kind inside type_info."""
    if type_info['kind'] == 'unknown':
        found.append((path, type_info['spelling']))
    for inner in type_info.get('inner', []) + type_info.get('returns', []):
        walk_types(inner, path, found)


def walk_signature(sig, path, found):
    """Collects the unknown kinds of a signature's parameters and returns."""
    for param in sig['params']:
        walk_types(param['type'], f'{path}({param["name"]})', found)
    for returned in sig['returns']:
        walk_types(returned, f'{path} -> ', found)


def unknown_types(digger):
    """Unclassified types in all compiled bindings (doc-only entries are text only and listed apart)."""
    found = []
    for function in digger['functions']:
        if not function.get('doc_only'):
            for overload in function['overloads']:
                walk_signature(overload, '.'.join(function.get('owner', []) + [function['name']]), found)
    for value in digger['values']:
        if not value.get('doc_only'):
            walk_types(value['type'], '.'.join(value.get('owner', []) + [value['name']]), found)
    for usertype in digger['usertypes']:
        for member in usertype['members']:
            path = f'{usertype["name"]}.{member["name"]}'
            if 'type' in member:
                walk_types(member['type'], path, found)
            for sig in member.get('overloads', []) + [member[s] for s in ('getter', 'setter') if s in member]:
                walk_signature(sig, path, found)
    return found


def accepted_reason(name, message):
    """Why the difference is accepted, or None."""
    if name in ACCEPTED:
        return ACCEPTED[name]
    for pattern, prefix, reason in ACCEPTED_PATTERNS:
        if re.match(pattern, name) and message.startswith(prefix):
            return reason
    return None


class Report:
    """Collects the differences, split into unexplained ones and accepted ones."""

    def __init__(self):
        self.problems = []
        self.accepted = []

    def add(self, name, message):
        """Records one difference for name; accepted when ACCEPTED or ACCEPTED_PATTERNS explain it."""
        reason = accepted_reason(name, message)
        if reason:
            self.accepted.append(f'{name}: {message} (accepted: {reason})')
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


def member_doc_candidates(member):
    """The doc texts a digger member may be compared with: the old parser joins the C++ declaration's comment and
    the binding's comment, or uses one of them."""
    binding = member.get('doc', [])
    declared = [member.get('cpp_doc', [])]
    for sig in member.get('overloads', []) + [member[s] for s in ('getter', 'setter') if s in member]:
        declared.append(sig.get('doc', []))
    candidates = [binding]
    for decl in declared:
        candidates += [decl, decl + binding, binding + decl]
    return [doc_text(c) for c in candidates]


def check_members(report, old_type, usertype):
    """Compares the members of one old usertype with the digger's."""
    members = {}
    for member in usertype['members']:
        members.setdefault(member['name'], []).append(member)
    for var in old_type['vars']:
        name = f'{old_type["name"]}.{var["name"]}'
        if var['name'].startswith(VTABLE_MEMBERS):
            continue
        found = members.get(var['name'])
        if not found and var['name'] == 'new' and usertype.get('constructors'):
            continue  # `"new", sol::initializers(...)`: the digger records it as the usertype's constructors
        if not found:
            report.add(name, 'member missing in the digger output')
            continue
        old_function = bool(var.get('function'))
        if not any((m['kind'] == 'method') == old_function for m in found):
            report.add(name, f'kind: old {"function" if old_function else "value"}, digger {found[0]["kind"]}')
        old = old_doc(var.get('comment', []))
        if old and not any(old in member_doc_candidates(m) for m in found):
            report.add(name, f'doc differs:\n      old:    {old}\n      digger: {member_doc_candidates(found[0])[:2]}')


def check_constructors(report, ps, usertype):
    """Compares constructor counts and parameter counts with ps.constructors."""
    old = ps.constructors.get(usertype['cpp'], [])
    new = usertype.get('constructors', [])
    old_counts = sorted(count_params(ps, c['signature']) for c in old)
    new_counts = sorted(len(c['params']) for c in new)
    if old and old_counts != new_counts:
        report.add(f'{usertype["name"]} constructors', f'param counts: old {old_counts}, digger {new_counts}')


def check_usertypes(report, ps, digger):
    """Compares every usertype of the old parser with the digger's."""
    by_name = {u['name']: u for u in digger['usertypes']}
    for old_type in ps.types:
        usertype = by_name.get(old_type['name'])
        if usertype is None:
            report.add(old_type['name'], 'usertype missing in the digger output')
            continue
        old_bases = re.sub(r'>.*$', '', old_type['base'])  # the old regex keeps the `>()` after the list
        if old_bases and old_bases.split(',') != usertype.get('bases', []):
            report.add(old_type['name'], f'bases: old {old_type["base"]}, digger {usertype.get("bases", [])}')
        old = old_doc(old_type.get('comment', []))
        if old and old != doc_text(usertype.get('doc', [])):
            report.add(old_type['name'], f'doc differs:\n      old:    {old}\n      digger: {usertype.get("doc", [])}')
        check_constructors(report, ps, usertype)
        check_members(report, old_type, usertype)


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
    check_usertypes(report, ps, digger)

    unknown = unknown_types(digger)
    doc_only = sorted(f['name'] for f in digger['functions'] + digger['values'] if f.get('doc_only'))
    print(f'old parser: {len(ps.funcs)} functions + {len(ps.deprecated_funcs)} deprecated')
    print(f'old parser usertypes: {len(ps.types)}, with {sum(len(t["vars"]) for t in ps.types)} members')
    print(f'digger: {len(digger["functions"])} functions ({len(globals_by_name)} global names), '
          f'{len(digger["values"])} values, {len(digger["usertypes"])} usertypes')
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
