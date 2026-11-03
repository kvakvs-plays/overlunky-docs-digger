"""Parity report: the digger's JSON against overlunky's old regex parser (docs/parse_source.py) as the oracle.

Usage: python tools/parity.py <overlunky root> <docs-digger JSON>

Checks every global function the old parser reports (ps.funcs, ps.deprecated_funcs):
  presence, deprecated flag, parameter counts and documentation text;
every usertype (ps.types, ps.constructors):
  presence, bases, type comment, constructors, and per member presence, kind (function or not) and docs;
every enum (ps.enums): presence, comment, and per value presence, value and block docs;
every alias, library and entity cast (ps.aliases, ps.lualibs, ps.known_casts) for presence;
the events (ps.events), and the global variables against the old hardcoded not_functions list;
the vtable hook members (P7: set_pre_<name> / set_post_<name> names and NoDoc flags);
the group of every old function against generate.py's func_cats (P14);
and lists every type the digger left unclassified. The callback report (DIG-4) counts callback parameters and events
per origin and lists the unresolved ones and the conflicts (doc fixes for overlunky, not failures). Differences that are understood are listed in ACCEPTED and
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
    (r'GAME_MODE$', 'enum doc differs',
     'the old parser keeps `///` lines across code lines, so the GAME_MODE table gets the comments of the Online '
     'and OnlinePlayer usertypes registered above it; the table itself has no comment'),
    (r'__get_script_envs$', 'event missing',
     'an undocumented read in C++ (`auto get_script_envs = lua["__get_script_envs"];`), not a global that scripts '
     'define'),
    (r'pause$', 'global variable not in the old not_functions list',
     'a PauseAPI object bound and documented as a global variable; the old hardcoded list misses it'),
    (r'ENTITY_OVERRIDE\.TRIGGER_EXPLOSION$', 'value',
     'two vtables name an entry trigger_explosion (Backpack 99, DummyPurchasableEntity 94); the old list keeps both, '
     'the Lua table holds one key, and the running game reports 94'),
    (r'.*', 'event: right side of an alias',
     'the old event regex also matches `lua["b"];` at the end of `lua["a"] = lua["b"];`: b is a Lua function that '
     'the digger reports as the target of alias a'),
    (r'CustomTheme\.\w+$', 'member missing',
     'level_lua.cpp binds these CustomTheme members inside a /* ... */ block comment, which the old text parser still '
     'reads; they are not bound (Lua reaches them through the ThemeInfo base)'),
]

# What the report checks, by parity item of the plan (.agents/01-plan.md, section 1).
COVERAGE = [
    ('P1', 'global functions: presence, parameter counts, docs'),
    ('P2', 'namespaced and member functions: usertype members (the old member_funs)'),
    ('P3', 'deprecated flags'),
    ('P4', 'NoDoc (old lists skip those) and the directive text kept in docs'),
    ('P5', 'usertypes: bases, docs, constructors, members and their kinds'),
    ('P6', 'C++ member docs (cpp_doc of members and getters)'),
    ('P7', 'vtable hook members and their NoDoc'),
    ('P8', 'events: documented reads of script globals'),
    ('P9', 'enums: tables, docs, values (numbers or text) and block docs'),
    ('P10', 'entity casts'),
    ('P11', 'libraries'),
    ('P12', 'aliases and their types'),
    ('P13', 'global variables against not_functions'),
    ('P14', 'function groups against generate.py func_cats'),
    ('P15', 'callback report: origins, unresolved callbacks, conflicts with the prose'),
    ('P16', 'not compared: the C++ to Lua type names are applied by the output stage'),
]

# Usertype members the old parser generates for vtable hooks; checked by check_vtable_hooks (P7).
VTABLE_MEMBERS = ('set_pre_', 'set_post_', 'clear_virtual')

# sol2 types of a Lua function parameter, which must get a callback signature.
LUA_FUNCTION_TYPES = ('sol::basic_function<', 'sol::basic_protected_function<')

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


def old_groups(overlunky_root, ps):
    """The group generate.py gives each old function, by name: its func_cats loop runs alone on ps.funcs (the rest of
    generate.py writes the markdown files)."""
    with open(os.path.join(overlunky_root, 'docs', 'generate.py'), encoding='utf-8') as f:
        text = f.read()
    scope = {'ps': ps}
    exec(text[text.index('func_cats = dict()'):text.index('for cat in sorted(func_cats):')], scope)
    return {func['name']: group for group, funcs in scope['func_cats'].items() for func in funcs}


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


def old_block_doc(text):
    """Docs the old parser takes from a `/* NAME // KEY // doc */` block, joined with `<br/>`, as lines."""
    return old_doc([line for line in (text or '').split('<br/>') if line.strip()])


def old_enum_number(text):
    """The old value text as a number when it is an integer literal, else None."""
    try:
        return int(text, 0)
    except ValueError:
        return None


def digger_value_text(value):
    """The digger's value text as the old parser writes it: spaces and quotes removed, and for commented-out
    pairs the old markdown rewrites (`__` is a space, `\\[` and `\\]` are parentheses)."""
    text = re.sub(r'\s+', '', value['text']).strip('"')
    if value.get('origin') == 'doc_comment':
        text = text.replace('__', ' ').replace('\\[', '(').replace('\\]', ')')
    return text


def walk_types(type_info, path, found):
    """Collects (path, spelling) of every unknown kind inside type_info."""
    if type_info['kind'] == 'unknown':
        found.append((path, type_info['spelling']))
    for inner in type_info.get('inner', []) + type_info.get('returns', []):
        walk_types(inner, path, found)


def walk_signature(sig, path, found):
    """Collects the unknown kinds of a signature's parameters and returns, and of the callbacks found in code."""
    for param in sig.get('params', []):
        walk_types(param['type'], f'{path}({param["name"]})', found)
        callback = param.get('callback', {})
        if callback.get('origin') not in (None, 'doc_comment', 'unresolved'):
            walk_signature(callback, f'{path}({param["name"]}) callback', found)
    for returned in sig.get('returns', []):
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
    for entry in digger['aliases'] + digger['script_callbacks']:
        walk_types(entry['type'], entry['name'], found)
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
        old_block = old_block_doc(var.get('docs'))
        if old_block and not any(old_block in member_doc_candidates(m) for m in found):
            report.add(name, f'block doc differs:\n      old:    {old_block}\n      digger: {found[0].get("doc", [])}')


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


def check_enum_value(report, enum_name, var, values):
    """Compares one old enum value (name, value text, block docs) with the digger's values of that name."""
    name = f'{enum_name}.{var["name"]}'
    found = values.get(var['name'], [])
    if not found:
        report.add(name, 'enum value missing in the digger output')
        return
    number = old_enum_number(var['type'])
    if number is not None and not any(v.get('value') == number for v in found):
        report.add(name, f'value: old {var["type"]}, digger {[v.get("value", v["text"]) for v in found]}')
    elif number is None and not any(digger_value_text(v) == var['type'] for v in found):
        report.add(name, f'value text: old {var["type"]}, digger {[digger_value_text(v) for v in found]}')
    old = old_block_doc(var.get('docs'))
    if old and not any(doc_text(v.get('doc', [])) == old for v in found):
        report.add(name, f'doc differs:\n      old:    {old}\n      digger: {found[0].get("doc", [])}')


def check_enums(report, ps, digger):
    """Compares every old enum table: presence, comment and values."""
    by_name = {e['name']: e for e in digger['enums']}
    for old_enum in ps.enums:
        table = by_name.get(old_enum['name'])
        if table is None:
            report.add(old_enum['name'], 'enum missing in the digger output')
            continue
        old = old_doc(old_enum.get('comment', []))
        if old and old != doc_text(table.get('doc', [])):
            report.add(old_enum['name'], f'enum doc differs:\n      old:    {old}\n      digger: {table.get("doc", [])}')
        values = {}
        for value in table['values']:
            values.setdefault(value['name'], []).append(value)
        for var in old_enum['vars']:
            check_enum_value(report, old_enum['name'], var, values)


def check_lists(report, ps, digger):
    """Aliases, libraries and entity casts: every old entry is present."""
    aliases = {a['name']: a for a in digger['aliases']}
    for alias in ps.aliases:
        found = aliases.get(alias['name'])
        if found is None:
            report.add(alias['name'], 'alias missing in the digger output')
        elif found['type']['spelling'] != alias['type'].rstrip(';'):
            report.add(alias['name'], f'alias type: old {alias["type"]}, digger {found["type"]["spelling"]}')
    libraries = {name for call in digger['libraries'] for name in call['names']}
    for library in ps.lualibs:
        if library not in libraries:
            report.add(library, 'library missing in the digger output')
    entity = next((u for u in digger['usertypes'] if u['name'] == 'Entity'), {'members': []})
    casts = {m['name'] for m in entity['members']}
    for cast in ps.known_casts:
        if cast not in casts:
            report.add(f'Entity.{cast}', 'cast missing in the digger output')


def check_globals(report, ps, digger, values_in_digger):
    """Events (documented reads of script globals), global variables against not_functions, and the old functions
    that are values in the digger."""
    callbacks = {c['name']: c for c in digger['script_callbacks']}
    alias_targets = {f['alias_of'][-1] for f in digger['functions'] if len(f.get('alias_of', [])) == 1}
    for event in ps.events:
        found = callbacks.get(event['name'])
        if found is not None:
            if old_doc(event['comment']) != doc_text(found.get('doc', [])):
                report.add(event['name'], f'event doc differs:\n      old:    {event["comment"]}\n'
                                          f'      digger: {found.get("doc", [])}')
        elif event['name'] in alias_targets:
            report.add(event['name'], 'event: right side of an alias')
        else:
            report.add(event['name'], 'event missing in the digger output')
    found_globals = set(digger['global_variables'])
    for name in sorted(set(ps.not_functions) - found_globals):
        report.add(name, 'global variable missing in the digger output')
    for name in sorted(found_globals - set(ps.not_functions)):
        report.add(name, 'global variable not in the old not_functions list')
    for name in values_in_digger:
        if name not in found_globals and name not in callbacks:
            report.add(name, 'old function is a value in the digger, neither a global variable nor a callback')


def check_groups(report, groups, globals_by_name):
    """P14: every old function the digger has keeps its generate.py group."""
    for name, old in sorted(groups.items()):
        new = sorted({b['group'] for b in globals_by_name.get(name, [])})
        if new and old not in new:
            report.add(name, f'group: old {old}, digger {new}')


def check_vtable_hooks(report, ps, digger):
    """P7: every old set_pre_ / set_post_ member exists without NoDoc, and every documented digger hook is old."""
    by_name = {u['name']: u for u in digger['usertypes']}
    for old_type in ps.types:
        old = {v['name'] for v in old_type['vars'] if v['name'].startswith(VTABLE_MEMBERS)}
        if not old:
            continue
        members = by_name.get(old_type['name'], {'members': []})['members']
        hooks = {m['name']: m for m in members if m['name'].startswith(VTABLE_MEMBERS)}
        documented = {name for name, m in hooks.items() if not m.get('nodoc')}
        for name in sorted(old - set(hooks)):
            report.add(f'{old_type["name"]}.{name}', 'vtable hook missing in the digger output')
        for name in sorted((old & set(hooks)) - documented):
            report.add(f'{old_type["name"]}.{name}', 'vtable hook: old documented, digger NoDoc')
        for name in sorted(documented - old):
            report.add(f'{old_type["name"]}.{name}', 'vtable hook: digger documented, old has none')


def signatures(digger):
    """(path, signature) of every bound signature: functions, constructors and member accessors and overloads."""
    for function in digger['functions']:
        for sig in function['overloads']:
            yield '.'.join(function.get('owner', []) + [function['name']]), sig
    for usertype in digger['usertypes']:
        for sig in usertype.get('constructors', []):
            yield f'{usertype["name"]}.new', sig
        for member in usertype['members']:
            for sig in member.get('overloads', []) + [member[s] for s in ('getter', 'setter') if s in member]:
                yield f'{usertype["name"]}.{member["name"]}', sig


def callback_report(report, digger):
    """Callback parameters and events per origin, the unresolved ones and the conflicts; a Lua function parameter
    without a callback is a problem. Returns the lines to print."""
    origins, unresolved, conflicts = {}, [], []
    for path, sig in signatures(digger):
        for param in sig['params']:
            callback = param.get('callback')
            canonical = param['type'].get('canonical', param['type']['spelling'])
            if callback is None:
                if canonical.startswith(LUA_FUNCTION_TYPES):
                    report.add(f'{path}({param["name"]})', 'Lua function parameter without a callback')
                continue
            where = f'{path}({param["name"]})'
            origins[callback['origin']] = origins.get(callback['origin'], 0) + 1
            if 'unresolved' in callback:
                unresolved.append(f'{where}: {callback["unresolved"]}'
                                  + (' (prose used)' if callback['origin'] == 'doc_comment' else ''))
            conflicts += [f'{where}: {c}' for c in callback.get('conflicts', [])]
    events = {}
    for event in digger['events']:
        callback = event['callback']
        kind = 'unresolved' if callback['origin'] == 'unresolved' else ('generic' if event.get('generic') else 'sites')
        events[kind] = events.get(kind, 0) + 1
        where = f'{event["table"]}.{event["name"]} ({event["stored_in"]})'
        if kind == 'unresolved':
            unresolved.append(f'{where}: {callback["unresolved"]}')
        conflicts += [f'{where}: {c}' for c in callback.get('conflicts', [])]
    lines = [f'callback parameters by origin: {dict(sorted(origins.items()))}',
             f'events: {len(digger["events"])} {dict(sorted(events.items()))}',
             f'\nunresolved callbacks: {len(unresolved)}'] + [f'  {u}' for u in unresolved]
    lines += [f'\ncallback conflicts (doc fixes for overlunky): {len(conflicts)}'] + [f'  {c}' for c in conflicts]
    return lines


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
            values_in_digger.append(func['name'])  # not callable: checked as a global variable or callback
            continue
        check_function(report, ps, func, deprecated, globals_by_name.get(func['name'], []))
    check_usertypes(report, ps, digger)
    check_enums(report, ps, digger)
    check_lists(report, ps, digger)
    check_globals(report, ps, digger, values_in_digger)
    check_vtable_hooks(report, ps, digger)
    groups = old_groups(overlunky_root, ps)
    check_groups(report, groups, globals_by_name)
    callbacks = callback_report(report, digger)

    unknown = unknown_types(digger)
    doc_only = sorted(f['name'] for f in digger['functions'] + digger['values'] if f.get('doc_only'))
    print('covered parity items:')
    for item, what in COVERAGE:
        print(f'  {item}: {what}')
    print(f'\nold parser: {len(ps.funcs)} functions + {len(ps.deprecated_funcs)} deprecated')
    print(f'old parser usertypes: {len(ps.types)}, with {sum(len(t["vars"]) for t in ps.types)} members')
    print(f'old parser: {len(ps.enums)} enums, {len(ps.aliases)} aliases, {len(ps.lualibs)} libraries, '
          f'{len(ps.known_casts)} casts, {len(ps.events)} events')
    print(f'digger: {len(digger["functions"])} functions ({len(globals_by_name)} global names), '
          f'{len(digger["values"])} values, {len(digger["usertypes"])} usertypes, {len(digger["enums"])} enums, '
          f'{len(digger["aliases"])} aliases, {len(digger["script_callbacks"])} script callbacks')
    print(f'global variables: {digger["global_variables"]}')
    print(f'groups: {len(groups)} old functions in {len(set(groups.values()))} groups compared')
    print(f'doc-only functions (text signatures, kinds not classified): {doc_only}')
    print()
    for line in callbacks:
        print(line)
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
