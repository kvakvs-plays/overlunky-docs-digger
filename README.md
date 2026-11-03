# docs-digger

`docs-digger` extracts the Lua API of [Overlunky](https://github.com/spelunky-fyi/overlunky) from its C++ sources.
It parses them with Clang LibTooling, using overlunky's own `compile_commands.json`, and writes one JSON file: every
Lua function, value, usertype, enum, alias, library, event and callback signature, with source locations,
documentation and exact C++ types. It replaces the regex-based extraction in overlunky's `docs/parse_source.py`,
finds everything that one finds, and adds what regexes cannot see: exact parameter and return types, nullable
parameters, and the signatures of Lua callbacks (what they receive, what they may return, and what `nil` means).

From that JSON it also writes the Lua definition files for the Lua language server (`spel2.lua` and friends) and
builds the HTML reference with search (`site.bat`), which replace overlunky's `generate_emmylua.py` and its Slate page.
The work plan and the design notes are in `.agents/01-plan.md` (finished steps in `.agents/00-finished.md`);
`AGENTS.md` has the rules for changing the code.

## Setup (Windows)

overlunky only parses on Windows (MSVC STL, Windows SDK), so the digger runs there.

```powershell
winget install -e --id Kitware.CMake
winget install -e --id Ninja-build.Ninja
winget install -e --id Git.Git
winget install -e --id Python.Python.3.12
# clang-cl and lld-link of the same major as the pinned LLVM (23); the scripts use C:\Program Files\LLVM\bin
winget install -e --id LLVM.LLVM
# MSVC STL + Windows SDK (skip if Visual Studio already has "Desktop development with C++")
winget install -e --id Microsoft.VisualStudio.2022.BuildTools --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

Instead of installing LLVM you may set `DD_LLVM_BIN` to the `bin` directory of the pinned package (see below) once
`cmake -DTHIRDPARTY_DIR=<repo>/thirdparty -P cmake/LLVMPackage.cmake` has downloaded it; CI does that.

The Clang/LLVM development libraries are not available from winget. The first configure downloads the pinned
`clang+llvm-<version>-x86_64-pc-windows-msvc.tar.xz` release (~860 MB, SHA256-checked, pinned in
`cmake/LLVMPackage.cmake`) and unpacks it into `thirdparty/` (git-ignored, about 4 GB). Later configures reuse it. To
use another LLVM install, pass `-DClang_DIR=<llvm root>/lib/cmake/clang`. Everything else that is downloaded (zlib and
zstd for LLVM, SQLite, the Lua headers for sol2, the analyzer venv) also goes to `thirdparty/`, pinned by version and
SHA256, and so does the Node.js that builds the HTML reference (`cmake/NodePackage.cmake`, fetched by `site.bat`; no
Node install is needed). `build/` only holds output that a configure and build recreate.

An overlunky checkout with submodules is expected at `../Playlunky/submodules/overlunky`
(`git submodule update --init --recursive` in Playlunky). Elsewhere, set the environment variable `DD_OVERLUNKY_ROOT`
to the checkout before the first build. `dig.bat` and the tests both use it; the tests need overlunky's sol2 headers.

Linux is not set up: Clang would come from distro packages (`llvm-<ver>-dev`, `libclang-<ver>-dev`) with
`-DClang_DIR=...`, but overlunky itself only parses on Windows.

## Build, run, test

The scripts enter the Visual Studio environment themselves (`tools\dev-env.cmd`); run them from any shell.

- `build.bat [Debug|Release|All]`: builds `docs-digger` with clang-cl and Ninja Multi-Config in `build\digger`.
- `test.bat [Debug|Release|All]`: builds and runs the CTest suite.
- `dig.bat [options]`: builds Release, configures overlunky into `build\overlunky` for its `compile_commands.json`
  (configure only, nothing of overlunky is built) and runs the digger on all `src/game_api` translation units. It passes
  the runtime enum dump `docs/game_data/lua_enums.txt` (`--runtime-enums`), the function groups `config\groups.json`
  (`--groups`), the Lua type names `config\lua_types.json` (`--lua-types`), the text fragments `config\fragments`
  (`--fragments`) and the result cache
  `build\digger-cache.sqlite` (`--cache`), and writes the LuaLS definition files into `build\lua` (`--lua-out`, with
  `config\lua_prelude.lua` and the links of the published docs) and the classic EmmyLua ones into `build\lua-emmylua`
  (`--emmylua-out`). When lua-language-server is on `PATH` or comes with
  the VS Code Lua extension, it then checks those files and reports problems (the run does not fail on them). Its own
  arguments go to the digger, for example `dig.bat -o F:\out\api.json --jobs 8`; give `-o` an absolute path.
- `site.bat [generator] [json]`: builds the HTML reference from `build\api.json` into `build\site\starlight` and checks
  it (see "HTML reference").
- `quality.bat [changed|all]`: the quality gate (below).

`build\digger\Release\docs-digger.exe --help` lists all options. The 101 translation units are parsed in parallel
(`--jobs`, default all hardware threads; a sol2 unit takes up to 2 GB): about 30 s on 32 threads, about 7 minutes with
`--jobs 1`. A unit whose files (the source and everything it includes, compared by content) are unchanged since the
last run comes from the cache, so a run without source changes takes a few seconds; a new digger build, another
`--root` or another compile command parse again. `--no-cache` parses everything (and refreshes the cache); deleting
`build\` only costs one full run. The digger prints a line per finished unit and, every 10 s, the units still parsing.

## Output

One JSON object. Its fields are documented in `schema/docs-digger.schema.json` (JSON Schema 2020-12): every object,
field and enum value has a description there, and no other fields exist. In short:

| Field | Contents |
|---|---|
| `schema_version` | Version of the format; raised with every change to fields or their meaning. Currently 3. |
| `functions` | Lua functions: name, owner table, `group`, doc and directives, overloads with typed params and returns. |
| `values` | Lua values that are not functions (`players`, `state`), with their C++ type. |
| `usertypes` | `new_usertype<T>` types: `group` and `section`, bases, constructors, members (fields, properties, methods, vtable hooks). |
| `enums` | Constant tables, values from the code, commented-out examples and the runtime enum dump. |
| `aliases` | Typedef names Lua-visible types are written with. |
| `libraries` | `open_libraries(...)` calls. |
| `script_callbacks` | Documented globals that scripts define and C++ reads (`on_frame`). |
| `global_variables` | Names of the documented values on the state. |
| `events` | Callback signature per key of `ON`, `THEME_OVERRIDE` and other enum-keyed callbacks. |

Types keep their C++ spelling plus a `kind` (`int`, `optional`, `container`, `function`, ...), inner types,
`nullable` and `hidden` (parameters sol2 injects), and `lua`, the type in LuaLS notation (`integer`, `Entity?`,
`ENT_TYPE[]`, `table<string, integer>`, `fun(p1: Entity): boolean`). The Lua names come from the type facts: records
become their usertype's name, enums and typedefs named like a Lua enum table or an alias keep that name; the few
types the facts cannot name are listed in `config/lua_types.json` (`{"names": {"ImVec2": "Vec2"}}`, keyed by canonical
type, spelling or unqualified name). A parameter that receives a Lua function has a `callback`: its params, returns,
`nil_allowed`, `nil_meaning` and how it was found (`origin`).

`python tools\check_schema.py schema\docs-digger.schema.json <output.json>` checks an output against the schema
(standard library only). The tests run it on every golden file and CI on the real output, so a field that the schema
does not describe fails. When the output changes, update the schema in the same change and raise `schema_version`
(`SchemaVersion` in `src/model.hpp` and the `const` in the schema).

### Lua definition files

`--lua-out <dir>` writes `---@meta` files for the Lua language server (the VS Code Lua extension), which replace
overlunky's `docs/game_data/spel2.lua`: `spel2.lua` (global variables, script callbacks, functions by group, aliases),
`spel2_types.lua` (one class per usertype: fields, operators, constructors as `Name.new`, methods as `Name:method`)
and `spel2_enums.lua`. A file that would grow past `--lua-max-file-size` (default 480,000 bytes, under the 500 KB
LuaLS loads by default) continues in `_2`, `_3`, cut between groups or types. Parameters that receive a Lua function
are typed as `fun(...)` with the callback's arguments and returns, and the meaning of a nil return in the
description. `/// NoDoc` items are left out; a NoDoc usertype that documented members return stays as a class without
members. `--lua-prelude <file>` adds hand-written declarations (`config/lua_prelude.lua`: `json`, `inspect`, the
`Meta` class); a global or function it declares replaces the generated one. `--lua-doc-url <url>` makes the
`(#anchor)` links in doc comments absolute.

Enum tables of bit flags (values written as `1 << n`, or powers of two) are marked `flags` in the JSON, each value
with its `bit` or the single-bit keys it `combines`; the Lua files and the site list them by bit, in hex. With
`--fragments <dir>` (`config/fragments`), the Markdown fragment `bit_flags.md` becomes each flag table's `usage`
text, its `{{enum}}`, `{{a}}`, `{{b}}`, `{{a_bit}}` and `{{a_number}}` filled from the table's two lowest bits: the
Lua files write it after the table docs and the site shows it above the values.

`--lua-style` picks the annotation dialect: `luals` (default) for lua-language-server, or `emmylua` for the more
limited classic EmmyLua of older IDE plugins (`T|nil` instead of `T?` and `name?`, `table` instead of tuple types,
`---@vararg`, descriptions after `@`, no `---@operator` or index fields). `--emmylua-out <dir>` writes an EmmyLua set
next to the `--lua-out` one in the same run (the HTML reference offers both for download).

Enums are written as plain tables with `---@alias NAME integer`, as the old file did: a `---@enum` parameter rejects
plain numbers (computed entity ids), and the literal values let LuaLS tell `ON.FRAME` from `ON.LEVEL`. Callbacks chosen
by an enum value (`set_callback(cb, ON.X)`, `CustomTheme:override`) get one `---@overload` per event with that value
as a literal type, so the callback's parameters are typed for the event passed. These overloads leave the callback's
return type out: lua-language-server only selects an overload by a function argument when its `fun` type has none.

### Function and type groups

Every function has a `group`, the documentation section it is listed in. The first of these decides:

1. a `/// Group: <name>` line in its doc comment (anywhere in the comment; it is taken out of the text);
2. the table it is bound in, for functions of namespace tables (`TYPE_MAP`, `io`);
3. the first rule of the `--groups` file that matches;
4. that file's `fallback`;
5. the source file name without `_lua` (`spawn_lua.cpp` gives `spawn`), when no file or fallback is given.

`config/groups.json` is ported from the `func_cats` table in overlunky's `docs/generate.py`. A rule matches when any
of its tests does:

```json
{
  "fallback": "Generic functions",
  "rules": [
    {"group": "Spawn functions", "name_contains": ["spawn"], "names": ["door", "layer_door"]},
    {"group": "Game patching functions", "file_contains": ["game_patches_lua.cpp"]}
  ]
}
```

`name_contains` and `file_contains` are substrings of the Lua name and of the source path relative to `--root`;
`names` are whole Lua names.

Every usertype gets a `group` and a `section` the same way (directive, rules, fallback, file name) from the `types`
object of the file, ported from `type_cats` in `generate.py`. A type rule applies only when one of the C++ bases
contains a `requires_base` substring (if any are given), and then matches when any of its tests does:

```json
"types": {
  "fallback": {"group": "Generic types", "section": "Non-Entity types"},
  "rules": [
    {"group": "Floor entities", "section": "Entity types", "bases_contain": ["Floor"], "names": ["Floor"]},
    {"group": "Logical entities", "section": "Entity types", "requires_base": ["Entity"], "name_contains": ["Logical"]}
  ]
}
```

`bases_contain` are substrings of a C++ base from `sol::bases<...>`. Generators use the groups for page grouping, so
another generator gets the same structure without its own rules.

## HTML reference

`site.bat [generator] [json]` builds the HTML reference from the digger JSON (default `build\api.json`) with a generator
of `src/html/<generator>/` (default `starlight`: [Astro Starlight](https://starlight.astro.build) with Pagefind search),
into `build\site\<generator>`. It takes the examples from overlunky's `docs/examples/` (`DD_OVERLUNKY_ROOT`, as
`dig.bat`) and from docs-digger's own [`examples/`](examples/README.md) (`<kind>/<name>`, at most
`<kind>/<owner>/<member>`: `types/Entity/destroy.lua`; checked by luacheck and by the build, which fails on a file
that names no symbol), and the Lua definition files of `build\lua` and `build\lua-emmylua`, which the home page
offers to view, to download one by one or as a ZIP per dialect (`tools/make_zip.py`: the same bytes on every build).
The pinned Node.js runs `npm ci` into `thirdparty\node-<generator>` when the lockfile changes, linked into the
generator directory as `node_modules`. A build takes about 15 s plus the checks. The site is served under
`DD_SITE_BASE` (default `/overlunky`) of `DD_SITE_URL` (default `https://spelunky-fyi.github.io`); source links go to
`DD_SITE_SOURCE_URL`.

Pages: home (with the downloads), Lua libraries, global variables, STD containers, one page per function group (the
groups of the JSON) and deprecated functions, one per usertype (bases, derived types, constructors, fields, methods),
the types by section and group, one per enum, the events of `ON` and `THEME_OVERRIDE` with their callback signatures,
casting and aliases. Every type name in a signature links to its page. The old single page's anchors keep working:
`index.html#spawn_entity` lands on the home page, whose script looks the anchor up in `old-anchors.json` and moves on;
the same map resolves the `(#anchor)` links of the doc comments (a link to an anchor nothing has stays as text and is
named at the end of the build: a doc fix for overlunky).

Search weighs a symbol's own page above mentions: each page lists the symbols it defines in a Pagefind meta field
(`SearchSymbols.astro`), signatures are left out of the index, and the ranking is tuned on the sample list
(`src/html/starlight/src/lib/ranking.mjs`).

After the build `site.bat` checks the site:

- `tools/check_site.py`: every internal link and `#fragment` resolves; every sample of `tools/site_samples.json` that the
  old page had a heading for lands on its symbol through `old-anchors.json`; every heading anchor (h1 to h3) of
  overlunky's old `docs/index.html` is mapped (`--compare <dir>` also compares two builds byte for byte);
- `npm run check` of the generator: the search check (`scripts/check-search.mjs` runs Pagefind's own search code over
  the built index): each sample's page is the first result, with the symbol's heading as its best sub-result. Samples
  with an `accepted` reason may miss and are listed with their rank (`DD_SEARCH_RANKING` tries other weights).

A generator is self-contained in its directory: its only inputs are the digger JSON, the examples and the staged
downloads; grouping comes from the JSON, the sample list is plain data in `tools/`. The quality gate skips `src/html/`.

## Tests

`test.bat` runs two tests per fixture:

- `golden.<name>` runs the digger on `tests/fixtures/<name>.cpp` (small sources using overlunky's real sol2 headers) and
  compares the JSON with `tests/expected/<name>.json` and the Lua files with `tests/expected/lua/<name>/`. A
  `<name>.lua_enums.txt` next to the fixture is passed as `--runtime-enums`, a `<name>.groups.json` as `--groups`. The
  fixture then runs through the result cache twice (fill, then read), and both outputs must be the same.
- `schema.<name>` checks the golden file against the schema.

Variants of a fixture with other options have their own Lua golden files
(`dd_add_golden_test(usertypes_split FIXTURE usertypes ARGS --lua-max-file-size=800)`; also `callbacks_emmylua`).
`luacheck` checks all golden Lua files with `tests/.luacheckrc` (settings for definition files); it needs Lua and
luacheck from LuaRocks (`luarocks install luacheck`) and is skipped without them.

`unit` runs the GoogleTest unit tests of pure helper logic (`tests/unit/`, the Lua type names); they link
`docs-digger-lib`, which holds all of the digger but `main()`. GoogleTest is pinned in `cmake/GoogleTest.cmake`.

After an intended output change, run `set DD_UPDATE_GOLDEN=1` and then `test.bat`, and review the diff of
`tests/expected/`. Every newly covered binding or callback pattern gets a fixture and a golden file in the same change.
Register new fixtures with `dd_add_golden_test(<name>)` in `tests/CMakeLists.txt`.

## Parity with the old parser

`python tools\parity.py <overlunky root> <output.json> [build\lua]` runs overlunky's old `docs/parse_source.py` and
compares its results with the digger's: functions (presence, deprecation, parameter counts, docs), usertypes and
members, enums, aliases, libraries, casts, events, global variables, vtable hooks, and the group of every function
against `generate.py`'s `func_cats`. Given the Lua directory, it also checks that every function, class, member, enum
value and alias of the old `docs/game_data/spel2.lua` is declared in the new files; given a built site as well
(`[build\lua build\site\starlight]`), that every heading anchor of the old `docs/index.html` is mapped. It also
compares every usertype's section and group with `generate.py`'s `type_cats`. It prints which parity items (P1 to P16
of the plan) it covers, lists the accepted
differences with their reasons, and exits with 1 on anything unexplained or any unclassified type. Its callback report
counts callback signatures per origin and lists the unresolved ones and the conflicts between the code and the
`The callback signature is ...` / `Params:` docs: doc fixes for overlunky, not failures. Run it at the end of every
plan step.

## Quality gate

`quality.bat` runs clang-format (all of `src/` and `tests/` except `tests/fixtures/`, whose line numbers are part of
the golden files), Lizard (cyclomatic complexity at most 10 per function) and clang-tidy (`.clang-tidy`: analyzer,
bugprone, performance and cognitive complexity at most 10, warnings as errors) over the files changed since `HEAD` and
the units that include a changed header; `quality.bat all` checks everything. It creates `thirdparty\venv-quality` with
the pinned analyzers (`tools/requirements-quality.txt`) and a configure-only build in `build\quality` on first use.
Format files you touch with `thirdparty\venv-quality\Scripts\clang-format.exe -i --style=file <files>`.

## CI

`.github/workflows/dig.yml` runs on `windows-latest`. For now it only runs when started by hand (`workflow_dispatch`,
with the overlunky repository and ref to dig; default `spelunky-fyi/overlunky` `main`): the outputs are built locally
with `dig.bat` and `site.bat`, and the workflow is kept up to date with them. It
fetches the LLVM package (cached with the other downloads in `thirdparty/`), builds with the package's clang-cl, runs
the tests, digs a fresh overlunky checkout, checks the output against the schema and uploads three artifacts:
`docs-digger-api` (`docs-digger-api.json` and the schema), `docs-digger-lua` (`lua/` LuaLS and `lua-emmylua/` definition
files) and `parity-report` (informative; a newer overlunky may bring differences the report does not explain yet).
The `site` job then builds and checks the HTML reference from those artifacts (with overlunky's `docs/` only) and
uploads it as `docs-digger-site`. Where the site is hosted is not decided yet, so nothing publishes it; set
`DD_SITE_URL` and `DD_SITE_BASE` in that job when it is.

The job lives in this repository, next to the digger and its pins, so overlunky does not need to build it. overlunky's
Slate job (`build-docs.yml`, on Ubuntu) can take the JSON of the latest successful run:

```sh
run=$(gh run list -R kvakvs-plays/overlunky-docs-digger -w dig.yml -b master -s success -L 1 --json databaseId -q '.[0].databaseId')
gh run download "$run" -R kvakvs-plays/overlunky-docs-digger -n docs-digger-api -D docs-digger
```

(`GH_TOKEN` must be set; artifacts are kept for 90 days.)

## Repository layout

```
CMakeLists.txt            the digger; tests; quality targets
build.bat, test.bat, dig.bat, site.bat, quality.bat
cmake/                    pinned downloads (LLVMPackage.cmake, NodePackage.cmake, Download.cmake), zlib/zstd/SQLite
                          builds, overlunky configure, quality checks
config/                   function and type group rules, Lua type names, Lua prelude
schema/                   JSON Schema of the output
src/main.cpp              command line, steps after all translation units, output
src/runner.cpp            parallel parsing, progress, ordered merge; cache.cpp and codec.cpp: the result cache
src/bindings.cpp          AST matchers: lua[...] = ..., enum tables, libraries, script callback reads
src/usertypes.cpp         new_usertype and its members
src/callbacks.cpp         callback flows, dispatch sites, vtable hooks (AST); callback_join.cpp joins them
src/resolve.cpp, types.cpp, comments.cpp   sol2 expressions, type classification, comments and directives
src/merge.cpp, groups.cpp, lua_names.cpp   steps after all translation units (no AST)
src/emit_lua.cpp          the LuaLS / EmmyLua definition files
src/model.hpp/.cpp        the data model and its JSON
src/html/starlight/       the HTML reference generator (Astro Starlight project; package.json and lockfile pinned)
tests/                    fixtures, golden JSON and Lua files, unit tests, RunGolden.cmake, RunLuacheck.cmake
tools/                    parity.py, check_schema.py, check_site.py, site_samples.json, make_zip.py, dev-env.cmd,
                          analyzer pins
```
