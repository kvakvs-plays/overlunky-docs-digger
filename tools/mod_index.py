"""Index where a Lua mod uses the overlunky API (functions, usertype members, enums, events) and pick example
candidates from those uses. Works on the digger JSON (build/api.json), so the names match the reference.

Stages (see .agents/hdmod-plan.md):
  scan   - certain hits + root groups (local variables whose type an agent has to name), split into batches
  merge  - typed roots from the agents + certain hits -> Markdown index
  select - example candidates (API functions/methods without an example) with source context, in batches
  check  - strict luacheck config and forbidden mod identifiers for the drafted examples
"""

import argparse
import json
import re
import sys
from collections import defaultdict
from pathlib import Path

# Lua keywords never start an API chain.
KEYWORDS = set(
    "and break do else elseif end false for function goto if in local nil not or repeat return then true until while".split()
)
# Lua standard and bundled libraries: their members are not usertype members.
LUA_LIBRARIES = set("math table string os io coroutine debug utf8 package json inspect serpent".split())
# Global API variables and the usertype each holds; `players` is indexed to Player.
GLOBAL_TYPES = {
    "state": "StateMemory",
    "savegame": "SaveData",
    "game_manager": "GameManager",
    "prng": "PRNG",
    "online": "Online",
    "players": "Player[]",
}
# Where the examples of overlunky live (first source of examples, not changed here).
OVERLUNKY_EXAMPLES = Path(__file__).resolve().parents[1] / ".." / "Playlunky" / "submodules" / "overlunky" / "docs" / "examples"
# Locations listed per symbol in the index; the rest is only counted.
MAX_LOCATIONS = 8
# Parsed command line (the API path is needed deep in the batch writer).
ARGS = None


class Api:
    """The parts of the digger JSON the scanner looks names up in."""

    def __init__(self, path):
        data = json.loads(Path(path).read_text(encoding="utf-8"))
        self.functions = {f["name"]: f for f in data["functions"] if re.fullmatch(r"\w+", f["name"])}
        self.enums = {e["name"]: {v["name"] for v in e["values"]} for e in data["enums"]}
        self.enums.setdefault("ON", set()).update(e["name"] for e in data["events"] if e.get("table") == "ON")
        self.types = {t["name"]: t for t in data["usertypes"]}
        self.members = {t["name"]: {m["name"]: m for m in t["members"]} for t in data["usertypes"]}
        self.bases = {t["name"]: t.get("bases", []) for t in data["usertypes"]}
        self.subclasses = defaultdict(list)
        for name, bases in self.bases.items():
            for base in bases:
                self.subclasses[base].append(name)
        self.member_owners = defaultdict(set)
        for tname, members in self.members.items():
            for mname in members:
                self.member_owners[mname].add(tname)

    def ancestors(self, tname):
        """The type and all its bases, nearest first: where an inherited member is declared."""
        out, todo = [], [tname]
        while todo:
            t = todo.pop(0)
            if t not in out:
                out.append(t)
                todo.extend(self.bases.get(t, []))
        return out

    def descendants(self, tname):
        """All subclasses of a type: a variable typed as a base may still use a member of a subclass."""
        out, todo = [], list(self.subclasses.get(tname, []))
        while todo:
            t = todo.pop(0)
            if t not in out:
                out.append(t)
                todo.extend(self.subclasses.get(t, []))
        return out

    def find_member(self, tname, mname):
        """Declaring type of a member used on a value of type tname, or None if ambiguous or unknown."""
        for t in self.ancestors(tname):
            if mname in self.members.get(t, {}):
                return t
        owners = [t for t in self.descendants(tname) if mname in self.members.get(t, {})]
        return owners[0] if len(owners) == 1 else None


def strip_lua(src):
    """Blank out comments and string contents (keeping quotes and newlines) so chains are found only in code."""
    out, i, n = [], 0, len(src)
    while i < n:
        c = src[i]
        if src.startswith("--", i):
            m = re.match(r"--\[(=*)\[", src[i:])
            end = src.find("]" + m.group(1) + "]", i) + len(m.group(1)) + 2 if m else src.find("\n", i)
            end = n if end < i + 2 else end
            out.append(re.sub(r"[^\n]", " ", src[i:end]))
            i = end
        elif c in "\"'":
            j = i + 1
            while j < n and src[j] != c and src[j] != "\n":
                j += 2 if src[j] == "\\" else 1
            out.append(c + re.sub(r"[^\n]", " ", src[i + 1 : j]) + c)
            i = j + 1
        elif m := re.match(r"\[(=*)\[", src[i : i + 64]):
            end = src.find("]" + m.group(1) + "]", i)
            end = n if end < 0 else end + len(m.group(1)) + 2
            out.append('"' + re.sub(r"[^\n]", " ", src[i + 1 : end - 1]) + '"')
            i = end
        else:
            out.append(c)
            i += 1
    return "".join(out)


IDENT = re.compile(r"[A-Za-z_]\w*")


def skip_balanced(text, i, open_c, close_c):
    """Index after the bracket that closes the one at i."""
    depth = 0
    for j in range(i, len(text)):
        if text[j] == open_c:
            depth += 1
        elif text[j] == close_c:
            depth -= 1
            if depth == 0:
                return j + 1
    return len(text)


def parse_chain(text, i):
    """Segments after a root identifier: ('.', name), (':', name), ('[]', None), ('()', None); and the end index."""
    segs = []
    while True:
        j = i
        while j < len(text) and text[j] in " \t":
            j += 1
        if j < len(text) and text[j] in ".:" and not text.startswith("..", j):
            m = IDENT.match(text, j + 1)
            if not m:
                return segs, i
            segs.append((text[j], m.group()))
            i = m.end()
        elif j < len(text) and text[j] == "[":
            segs.append(("[]", None))
            i = skip_balanced(text, j, "[", "]")
        elif j < len(text) and text[j] in "(\"'{" and segs and segs[-1][0] in ".:" or j < len(text) and text[j] == "(":
            segs.append(("()", None))
            i = skip_balanced(text, j, "(", ")") if text[j] == "(" else j + 1
        else:
            return segs, i


def strip_type(lua):
    """Usertype name in a JSON lua type spelling, with its container kind removed by indexing later."""
    return (lua or "").rstrip("?")


def index_type(lua):
    """Element type when a value of this type is indexed with [ ]."""
    lua = strip_type(lua)
    if lua.endswith("[]"):
        return lua[:-2]
    m = re.fullmatch(r"table<[^,]+,\s*(\w+)>", lua)
    return m.group(1) if m else None


def returns_of(entry):
    """Lua type of the first return value of a function or method entry."""
    for ov in entry.get("overloads", []):
        for r in ov.get("returns", []):
            return r.get("lua")
    return None


def member_type(member):
    """Lua type of a field or property, or the return type of a method."""
    if member["kind"] == "method":
        return returns_of(member)
    return member.get("type", {}).get("lua")


def walk(api, tname, segs, line, hits):
    """Follow a chain through member types from a known usertype, recording each member hit."""
    for kind, name in segs:
        if tname is None:
            return
        if kind == "[]":
            tname = index_type(tname)
            continue
        if kind == "()":
            continue
        owner = api.find_member(strip_type(tname), name)
        if owner is None:
            return
        hits.append(("member", f"{owner}.{name}" if kind == "." else f"{owner}:{name}", line))
        tname = member_type(api.members[owner][name])


class FileScan:
    """Hits and root groups of one Lua file."""

    def __init__(self, api, rel, src, shadowed):
        self.api, self.rel, self.shadowed = api, rel, shadowed
        self.text = strip_lua(src)
        self.line_starts = [0] + [m.end() for m in re.finditer(r"\n", self.text)]
        self.hits = []  # (symbol kind, symbol, line)
        self.roots = defaultdict(lambda: {"members": defaultdict(list), "hints": set()})

    def line_of(self, pos):
        """1-based line of a text offset."""
        lo, hi = 0, len(self.line_starts)
        while lo + 1 < hi:
            mid = (lo + hi) // 2
            if self.line_starts[mid] <= pos:
                lo = mid
            else:
                hi = mid
        return lo + 1

    def run(self):
        """Visit every chain root of the file (identifiers not preceded by `.`/`:` or glued to a number)."""
        for m in IDENT.finditer(self.text):
            k = m.start() - 1
            if k >= 0 and (self.text[k].isalnum() or self.text[k] == "_"):
                continue
            while k >= 0 and self.text[k] in " 	":
                k -= 1
            if k >= 0 and self.text[k] in ".:" and not self.text.startswith("..", k - 1):
                continue
            root = m.group()
            if root in KEYWORDS:
                continue
            segs, _ = parse_chain(self.text, m.end())
            self.classify(root, segs, self.line_of(m.start()), self.text[max(0, m.start() - 9) : m.start()])
        return self

    def classify(self, root, segs, line, before):
        """Record what a chain root names: an API function, enum, global, or a local to be typed."""
        api = self.api
        if re.search(r"\bfunction\s*$", before) or root in self.shadowed:
            return
        if root in api.functions and segs and segs[0][0] == "()":
            self.hits.append(("function", root, line))
            walk(api, returns_of(api.functions[root]), segs[1:], line, self.hits)
        elif root in api.enums and segs:
            if segs[0][0] == "." and segs[0][1] in api.enums[root]:
                self.hits.append(("enum", f"{root}.{segs[0][1]}", line))
            else:
                self.hits.append(("enum", root, line))
        elif root in GLOBAL_TYPES:
            self.hits.append(("global", root, line))
            walk(api, GLOBAL_TYPES[root], segs, line, self.hits)
        elif root in api.types and segs:
            walk(api, root, segs, line, self.hits)
        elif root in LUA_LIBRARIES:
            pass
        elif segs and segs[0][0] in ".:" and segs[0][1] in api.member_owners:
            group = self.roots[root]
            group["members"][segs[0][1]].append(line)
            group.setdefault("chains", []).append((line, segs))
        self.note_hint(root, segs, line)

    def note_hint(self, root, segs, line):
        """`local x = get_entity(...)`: the type a call result gives a variable (help for the typing agent)."""
        tail = self.text[self.line_starts[line - 1] : self.line_starts[line] if line < len(self.line_starts) else None]
        m = re.match(r"\s*(?:local\s+)?(\w+)\s*=\s*(\w+)\s*(\(|[.:]\s*new\s*\()", tail)
        if not m or m.group(2) != root:
            return
        if root in self.api.functions and m.group(3) == "(":
            ret = strip_type(returns_of(self.api.functions[root]))
            if ret in self.api.types:
                self.roots[m.group(1)]["hints"].add(f"{line}: = {root}() -> {ret}")
        elif root in self.api.types and m.group(3) != "(":
            self.roots[m.group(1)]["hints"].add(f"{line}: = {root}.new() -> {root}")


def mod_definitions(files):
    """Global names a mod defines itself (functions, tables): calls to them are not API uses."""
    names = set()
    for _, src in files:
        text = strip_lua(src)
        names.update(re.findall(r"^\s*function\s+(\w+)\s*\(", text, re.M))
        names.update(re.findall(r"^(\w+)\s*=", text, re.M))
        names.update(re.findall(r"^\s*local\s+function\s+(\w+)", text, re.M))
        names.update(re.findall(r"^\s*local\s+(\w+)\s*=\s*require", text, re.M))
    return names


def read_mod(root):
    """All Lua files of a mod as (relative path, source)."""
    out = []
    for p in sorted(Path(root).rglob("*.lua")):
        if ".git" not in p.parts:
            out.append((p.relative_to(root).as_posix(), p.read_text(encoding="utf-8", errors="replace")))
    return out


def scan(args):
    """Stage S1: certain hits and root groups, batched by line count for the typing agents."""
    api = Api(args.api)
    files = read_mod(args.mod)
    shadowed = mod_definitions(files) - set(GLOBAL_TYPES)
    if args.allow_shadow:
        shadowed -= set(api.functions)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    hits, groups = [], []
    for rel, src in files:
        fs = FileScan(api, rel, src, shadowed).run()
        hits += [(k, s, rel, ln) for k, s, ln in fs.hits]
        for root, g in sorted(fs.roots.items()):
            if g["members"]:
                groups.append(group_entry(api, rel, root, g, src.count("\n") + 1))
    (out / "hits.json").write_text(json.dumps(hits), encoding="utf-8")
    (out / "groups.json").write_text(json.dumps(groups, indent=1), encoding="utf-8")
    write_batches(out, files, groups, args.batches)
    print(f"{len(files)} files, {len(hits)} certain hits, {len(groups)} root groups")


def group_entry(api, rel, root, g, nlines):
    """One root group: the members a local uses, the types that declare them, hints, and the auto type."""
    members = {m: sorted(set(lines)) for m, lines in sorted(g["members"].items())}
    hinted = {h.rsplit("-> ", 1)[1] for h in g["hints"]}
    auto = None
    if len(hinted) == 1:
        t = next(iter(hinted))
        auto = t if all(api.find_member(t, m) for m in members) else None
    chains = [[ln, segs] for ln, segs in g.get("chains", [])]
    return {"file": rel, "root": root, "members": members, "hints": sorted(g["hints"]), "auto": auto, "chains": chains}


def group_line(api, g):
    """One root group as a compact text line for the typing agent."""
    mem = ", ".join(f"{m}@{','.join(map(str, ls[:6]))}[{'/'.join(sorted(api.member_owners[m]))}]" for m, ls in g["members"].items())
    hints = ("  hints: " + "; ".join(g["hints"])) if g["hints"] else ""
    return f"- `{g['root']}`: {mem}{hints}"


def write_batches(out, files, groups, count):
    """Split files into batches of about equal line count; each batch lists its untyped root groups for one agent."""
    api = Api(ARGS.api)
    sizes = sorted(((src.count("\n") + 1, rel) for rel, src in files), reverse=True)
    loads = [[0, []] for _ in range(count)]
    for size, rel in sizes:
        b = min(loads, key=lambda x: x[0])
        b[0] += size
        b[1].append(rel)
    for n, (lines, rels) in enumerate(loads, 1):
        mine = [g for g in groups if g["file"] in rels and not g["auto"]]
        text = [f"# Batch {n}: {len(rels)} files, {lines} lines, {len(mine)} root groups", ""]
        for rel in sorted(rels):
            rows = [group_line(api, g) for g in mine if g["file"] == rel]
            if rows:
                text += [f"## {rel}", *rows, ""]
        (out / f"batch-{n}.md").write_text("\n".join(text), encoding="utf-8", newline="\n")
        print(f"batch-{n}: {len(rels)} files, {lines} lines, {len(mine)} groups to type")


def span_type(spans, line):
    """The type an agent gave a root at a line, or None."""
    for s in spans:
        if s["from"] <= line <= s["to"]:
            return s["type"]
    return None


def collect(api, out):
    """All hits: certain (scan) and typed (agents), as symbol kind -> symbol -> sorted unique locations."""
    found = defaultdict(lambda: defaultdict(set))
    for k, s, rel, ln in json.loads((out / "hits.json").read_text(encoding="utf-8")):
        found[k][s].add((rel, ln))
    typed, unresolved, untyped = typed_member_hits(api, out)
    for k, s, rel, ln in typed:
        found[k][s].add((rel, ln))
    return found, unresolved, untyped


def typed_member_hits(api, out):
    """typed_hits with the file of every hit attached."""
    labels = {}
    for p in sorted(out.glob("types-*.json")):
        for t in json.loads(p.read_text(encoding="utf-8")):
            labels[(t["file"], t["root"])] = t["spans"]
    result, unresolved, untyped = [], defaultdict(set), 0
    for g in json.loads((out / "groups.json").read_text(encoding="utf-8")):
        spans = labels.get((g["file"], g["root"])) or ([{"from": 1, "to": 10**9, "type": g["auto"]}] if g["auto"] else None)
        untyped += spans is None
        for line, segs in g["chains"] if spans else []:
            tname = span_type(spans, line)
            if not tname or tname == "-":
                continue
            hits = []
            if tname in api.types:
                walk(api, tname, [tuple(s) for s in segs], line, hits)
            if not hits:
                unresolved[f"{tname} {segs[0][0]}{segs[0][1]}"].add(f"{g['file']}:{line}")
            result += [(k, s, g["file"], ln) for k, s, ln in hits]
    return result, unresolved, untyped


def locations(locs):
    """Up to MAX_LOCATIONS `file:line` entries of a symbol, then the count of the rest."""
    items = sorted(locs)
    text = ", ".join(f"`{f}:{ln}`" for f, ln in items[:MAX_LOCATIONS])
    return text + (f" (+{len(items) - MAX_LOCATIONS})" if len(items) > MAX_LOCATIONS else "")


def section(title, symbols, key=lambda s: s):
    """Markdown list of symbols with use counts and locations."""
    lines = [f"## {title} ({len(symbols)})", ""]
    lines += [f"- `{s}` {len(locs)}: {locations(locs)}" for s, locs in sorted(symbols.items(), key=lambda kv: key(kv[0]))]
    return lines + [""]


def merge(args):
    """Stage S3: write the Markdown index of all API uses."""
    api, out = Api(args.api), Path(args.out)
    found, unresolved, untyped = collect(api, out)
    enums = {s: l for s, l in found["enum"].items() if not s.startswith("ON.") and s != "ON"}
    events = {s: l for s, l in found["enum"].items() if s.startswith("ON.")}
    total = sum(len(v) for kind in found.values() for v in kind.values())
    doc = [
        "# HDMod API usage index",
        "",
        f"Generated by `tools/mod_index.py merge` from `{Path(args.mod).resolve().as_posix()}` against `{args.api}`.",
        f"{total} uses. Per symbol: number of distinct uses, then up to {MAX_LOCATIONS} `file:line` locations",
        "(paths relative to the mod root). Members are listed under their declaring type.",
        "Typed by the scanner (call results, globals, constructor hints) and by agents (local variables).",
        "",
    ]
    doc += section("Functions", found["function"])
    doc += section("Events", events)
    doc += section("Global variables", found["global"])
    doc += section("Usertype members", found["member"], key=lambda s: s.replace(":", "."))
    doc += section("Enum values", enums)
    doc += [f"## Unresolved ({len(unresolved)} chains, {untyped} root groups without a type)", ""]
    doc += [f"- `{k}`: {', '.join(sorted(v)[:4])}" for k, v in sorted(unresolved.items())]
    Path(args.index).write_text("\n".join(doc) + "\n", encoding="utf-8", newline="\n")
    print({k: len(v) for k, v in found.items()}, f"unresolved {len(unresolved)}, untyped {untyped}")


def existing_examples(examples):
    """Symbols that already have an example (overlunky docs/examples, digger examples/): no new one is drafted."""
    have = {p.stem for p in OVERLUNKY_EXAMPLES.glob("*.*")} if OVERLUNKY_EXAMPLES.exists() else set()
    for p in Path(examples).rglob("*.*"):
        rel = p.relative_to(examples).with_suffix("").parts
        if len(rel) >= 2:
            have.add(".".join(rel[1:]))
    return have


def example_path(api, symbol):
    """examples/ file for a symbol (README layout): functions/<name>.lua or types/<Type>/<member>.lua."""
    if "." not in symbol and ":" not in symbol:
        return f"functions/{symbol}.lua"
    t, m = re.split(r"[.:]", symbol)
    return f"types/{t}/{m}.lua"


def context(src_lines, line, around=4):
    """Source lines around a use, numbered."""
    lo, hi = max(1, line - around), min(len(src_lines), line + around)
    return "\n".join(f"{n:5}  {src_lines[n - 1]}" for n in range(lo, hi + 1))


def select(args):
    """Stage S4: API functions and methods used by the mod without an example, with source context, in batches."""
    api, out = Api(args.api), Path(args.out)
    found, _, _ = collect(api, out)
    have = existing_examples(args.examples)
    symbols = dict(found["function"])
    for s, locs in found["member"].items():
        t, m = re.split(r"[.:]", s)
        if api.members[t][m]["kind"] == "method":
            symbols[f"{t}:{m}"] = symbols.get(f"{t}:{m}", set()) | locs
    picked = [(s, l) for s, l in sorted(symbols.items()) if s not in EXCLUDE and s.replace(":", ".") not in have]
    picked = [(s, l) for s, l in picked if documented(api, f"examples/{example_path(api, s)}") is True]
    sources = {}
    batches = [picked[i : i + args.size] for i in range(0, len(picked), args.size)]
    for n, batch in enumerate(batches, 1):
        text = [f"# Example candidates {n}/{len(batches)}: {len(batch)} symbols", ""]
        for s, locs in batch:
            text += [f"## `{s}` -> `examples/{example_path(api, s)}` ({len(locs)} uses)", ""]
            for rel, ln in pick_locations(locs, args.per_symbol):
                src = sources.setdefault(rel, (Path(args.mod) / rel).read_text(encoding="utf-8", errors="replace").splitlines())
                text += [f"`{rel}:{ln}`", "```lua", context(src, ln), "```", ""]
        (out / f"examples-{n}.md").write_text("\n".join(text), encoding="utf-8", newline="\n")
    print(f"{len(picked)} symbols without examples in {len(batches)} batches ({len(have)} symbols have examples)")


# API names that get no example from a mod: `require` there only loads the mod's own modules.
EXCLUDE = {"require"}


def pick_locations(locs, count):
    """Up to count uses, each from another file where possible, so the drafter sees different contexts."""
    by_file = defaultdict(list)
    for rel, ln in sorted(locs):
        by_file[rel].append(ln)
    picked = [(rel, lines[0]) for rel, lines in sorted(by_file.items(), key=lambda kv: -len(kv[1]))]
    for rel, lines in by_file.items():
        picked += [(rel, ln) for ln in lines[1:]]
    return picked[:count]


def check(args):
    """Stage S6: strict luacheck config (API globals only) and the mod identifiers that must not show in examples."""
    api, out = Api(args.api), Path(args.out)
    names = set(api.functions) | set(api.enums) | set(api.types) | set(GLOBAL_TYPES) | {"options", "meta", "pause"}
    names -= {"require"}
    rc = ['std = "lua54"', "max_line_length = 120", "globals = {"] + [f'    "{n}",' for n in sorted(names)] + ["}", ""]
    (out / "strict.luacheckrc").write_text("\n".join(rc), encoding="utf-8", newline="\n")
    files = read_mod(args.mod)
    own = {n for n in mod_definitions(files) if len(n) >= 5} - names
    own |= {Path(rel).stem for rel, _ in files if len(Path(rel).stem) >= 5}
    # API member and enum value names (`state.logic`, `ENT_TYPE.X`) are not mod content even when a mod file shares them.
    own -= {m.lower() for m in api.member_owners} | {v.lower() for vals in api.enums.values() for v in vals}
    own -= set(api.functions)
    api_words = {n.lower() for n in names | set(api.member_owners)} | {v.lower() for vals in api.enums.values() for v in vals}
    # Short plain words (`doors`, `temple`) are mod file names too; only distinctive identifiers identify the mod. Its
    # monster/item/area names come from the curated --custom-names list instead.
    own = {n for n in own if n.lower() not in api_words and ("_" in n or n.endswith("lib") or len(n) >= 8)}
    words = sorted(own)
    (out / "forbidden.txt").write_text("\n".join(words) + "\n", encoding="utf-8", newline="\n")
    pattern = re.compile(r"\b(" + "|".join(map(re.escape, words)) + r")\b", re.I)
    custom = custom_names_pattern(args.custom_names)
    bad = 0
    root = Path(args.examples)
    import shutil

    shutil.rmtree(out / "md-blocks", ignore_errors=True)  # blocks of removed .md files must not be checked again
    for p in sorted(root.rglob("*.lua")):
        bad += check_code(api, root, p, p.read_text(encoding="utf-8"), pattern, custom)
    for p in sorted(f for f in root.rglob("*.md") if f.parent != root):
        bad += check_markdown(api, root, p, out, pattern, custom)
    bad += run_luacheck(sorted((out / "md-blocks").rglob("*.lua")), Path(args.examples) / ".luacheckrc")
    print(f"strict config: {out / 'strict.luacheckrc'}; {len(words)} mod identifiers; {bad} problems")


def custom_names_pattern(path):
    """Regex of a mod's own monster/item/area names (`_` also matching a space), or None without a list file."""
    if not path or not Path(path).exists():
        return None
    names = [n.strip() for n in Path(path).read_text(encoding="utf-8").splitlines() if n.strip() and not n.startswith("#")]
    return re.compile(r"\b(" + "|".join(re.escape(n).replace("_", "[_ ]") for n in names) + r")\b", re.I)


def check_code(api, root, p, code, pattern, custom):
    """Problems in example code: mod identifiers or custom names, unknown enum values, a target without a page."""
    bad = 0
    for n, line in enumerate(code.splitlines(), 1):
        m = (custom and custom.search(line)) or (pattern.search(line) if not line.lstrip().startswith("--") else None)
        if m:
            bad += 1
            print(f"{p.as_posix()}:{n}: mod name `{m.group()}`")
    if (why := documented(api, "examples/" + p.relative_to(root).as_posix())) is not True:
        bad += 1
        print(f"{p.as_posix()}: {why}")
    for enum, value in re.findall(r"\b([A-Z][A-Z_0-9]+)\.([A-Z_0-9]+)\b", strip_lua(code)):
        if enum in api.enums and value not in api.enums[enum]:
            bad += 1
            print(f"{p.as_posix()}: no enum value {enum}.{value}")
    return bad


def check_markdown(api, root, p, out, pattern, custom):
    """A Markdown example: custom names in its prose, its lua blocks as code (also saved for luacheck)."""
    text = p.read_text(encoding="utf-8")
    bad = 0
    for n, line in enumerate(text.splitlines(), 1):
        if re.match(r"\s*#", line) and not line.startswith("    "):
            bad += 1
            print(f"{p.as_posix()}:{n}: headings break the page outline; use a bold lead-in")
        if custom and (m := custom.search(line)):
            bad += 1
            print(f"{p.as_posix()}:{n}: mod name `{m.group()}`")
    blocks = re.findall(r"```lua\n(.*?)```", text, re.S)
    bad += check_code(api, root, p, "\n".join(blocks), pattern, None)
    rel = p.relative_to(root).with_suffix("")
    for i, block in enumerate(blocks, 1):
        dest = out / "md-blocks" / f"{rel.as_posix()}.{i}.lua"
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(block, encoding="utf-8", newline="\n")
    return bad


def run_luacheck(files, config):
    """luacheck (LuaRocks script under %APPDATA%) over files with the examples' config; the number of failing files."""
    if not files:
        return 0
    import os
    import subprocess

    tree = Path(os.environ.get("APPDATA", "")) / "luarocks"
    env = dict(os.environ, LUA_PATH=f"{tree}/share/lua/5.4/?.lua;{tree}/share/lua/5.4/?/init.lua;;", LUA_CPATH=f"{tree}/lib/lua/5.4/?.dll;;")
    cmd = ["lua", str(tree / "bin" / "luacheck"), "--config", str(config), "--no-color", "--quiet", "--formatter", "plain"]
    result = subprocess.run(cmd + [str(f) for f in files], env=env, capture_output=True, text=True)
    if result.stdout.strip():
        print(result.stdout.strip())
    return len({line.split(":")[0] for line in result.stdout.splitlines() if line.strip()})


def documented(api, path):
    """True when an examples/ file names a function or member the reference shows (not `nodoc`), else the reason."""
    parts = Path(path).with_suffix("").parts[1:]
    if parts[0] == "functions" and len(parts) == 2:
        f = api.functions.get(parts[1])
        return True if f and not f.get("nodoc") else "no such documented function"
    if parts[0] == "types" and len(parts) == 3:
        m = api.members.get(parts[1], {}).get(parts[2])
        return True if m and not m.get("nodoc") else "no such documented member"
    if parts[0] == "types" and len(parts) == 2:
        return True if parts[1] in api.types else "no such type"
    if parts[0] == "enums" and len(parts) == 2:
        return True if parts[1] in api.enums else "no such enum"
    if parts[0] == "events" and len(parts) == 3:
        return True if parts[2] in api.enums.get(parts[1], ()) else "no such event"
    return True if parts[0] in ("globals", "text") else "unknown layout"


def main():
    """Command line entry."""
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--api", default="build/api.json")
    ap.add_argument("--mod", required=True, help="mod root directory")
    ap.add_argument("--out", default="build/hdmod", help="work directory")
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("scan")
    s.add_argument("--batches", type=int, default=8)
    s.add_argument("--allow-shadow", action="store_true", help="count calls to API names the mod also defines")
    m = sub.add_parser("merge")
    m.add_argument("--index", default=".agents/hdmod-index.md")
    e = sub.add_parser("select")
    e.add_argument("--examples", default="examples")
    e.add_argument("--size", type=int, default=40, help="symbols per batch")
    e.add_argument("--per-symbol", type=int, default=3, help="uses shown per symbol")
    c = sub.add_parser("check")
    c.add_argument("--examples", default="examples")
    c.add_argument("--custom-names", default=".agents/hdmod-custom-names.txt", help="the mod's own monster/item/area names")
    args = ap.parse_args()
    global ARGS
    ARGS = args
    {"scan": scan, "merge": merge, "select": select, "check": check}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
