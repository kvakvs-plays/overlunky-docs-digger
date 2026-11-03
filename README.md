# docs-digger

`docs-digger` extracts the Lua API of [Overlunky](https://github.com/spelunky-fyi/overlunky) from its C++ sources.
It parses them with Clang LibTooling, using overlunky's own `compile_commands.json`, and writes one JSON file: every
Lua function, value, usertype, enum, alias, library, event and callback signature, with source locations,
documentation and exact C++ types. It replaces the regex-based extraction in overlunky's `docs/parse_source.py`,
finds everything that one finds, and adds what regexes cannot see: exact parameter and return types, nullable
parameters, and the signatures of Lua callbacks (what they receive, what they may return, and what `nil` means).

The output is data only; turning it into HTML pages or Lua type stubs is a later step. The work plan and the design
notes are in `.agents/01-plan.md` (finished steps in `.agents/00-finished.md`); `AGENTS.md` has the rules for changing
the code.

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
SHA256. `build/` only holds output that a configure and build recreate.

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
  (`--groups`), the Lua type names `config\lua_types.json` (`--lua-types`) and the result cache
  `build\digger-cache.sqlite` (`--cache`). Its own arguments go to the digger, for example
  `dig.bat -o F:\out\api.json --jobs 8`; give `-o` an absolute path.
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
| `schema_version` | Version of the format; raised with every change to fields or their meaning. Currently 2. |
| `functions` | Lua functions: name, owner table, `group`, doc and directives, overloads with typed params and returns. |
| `values` | Lua values that are not functions (`players`, `state`), with their C++ type. |
| `usertypes` | `new_usertype<T>` types: bases, constructors, members (fields, properties, methods, vtable hooks). |
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

### Function groups

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

## Tests

`test.bat` runs two tests per fixture:

- `golden.<name>` runs the digger on `tests/fixtures/<name>.cpp` (small sources using overlunky's real sol2 headers) and
  compares the output with `tests/expected/<name>.json`. A `<name>.lua_enums.txt` next to the fixture is passed as
  `--runtime-enums`, a `<name>.groups.json` as `--groups`. The fixture then runs through the result cache twice (fill,
  then read), and both outputs must be the same.
- `schema.<name>` checks the golden file against the schema.

`unit` runs the GoogleTest unit tests of pure helper logic (`tests/unit/`, the Lua type names); they link
`docs-digger-lib`, which holds all of the digger but `main()`. GoogleTest is pinned in `cmake/GoogleTest.cmake`.

After an intended output change, run `set DD_UPDATE_GOLDEN=1` and then `test.bat`, and review the diff of
`tests/expected/`. Every newly covered binding or callback pattern gets a fixture and a golden file in the same change.
Register new fixtures with `dd_add_golden_test(<name>)` in `tests/CMakeLists.txt`.

## Parity with the old parser

`python tools\parity.py <overlunky root> <output.json>` runs overlunky's old `docs/parse_source.py` and compares its
results with the digger's: functions (presence, deprecation, parameter counts, docs), usertypes and members, enums,
aliases, libraries, casts, events, global variables, vtable hooks, and the group of every function against
`generate.py`'s `func_cats`. It prints which parity items (P1 to P16 of the plan) it covers, lists the accepted
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

`.github/workflows/dig.yml` runs on `windows-latest` for pushes to `master`, pull requests and by hand
(`workflow_dispatch`, with the overlunky repository and ref to dig; default `spelunky-fyi/overlunky` `main`). It
fetches the LLVM package (cached with the other downloads in `thirdparty/`), builds with the package's clang-cl, runs
the tests, digs a fresh overlunky checkout, checks the output against the schema and uploads two artifacts:
`docs-digger-api` (`docs-digger-api.json` and the schema) and `parity-report` (informative; a newer overlunky may bring
differences the report does not explain yet).

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
build.bat, test.bat, dig.bat, quality.bat
cmake/                    pinned downloads (LLVMPackage.cmake, Download.cmake), zlib/zstd/SQLite builds,
                          overlunky configure, quality checks
config/                   function group rules, Lua type names
schema/                   JSON Schema of the output
src/main.cpp              command line, steps after all translation units, output
src/runner.cpp            parallel parsing, progress, ordered merge; cache.cpp and codec.cpp: the result cache
src/bindings.cpp          AST matchers: lua[...] = ..., enum tables, libraries, script callback reads
src/usertypes.cpp         new_usertype and its members
src/callbacks.cpp         callback flows, dispatch sites, vtable hooks (AST); callback_join.cpp joins them
src/resolve.cpp, types.cpp, comments.cpp   sol2 expressions, type classification, comments and directives
src/merge.cpp, groups.cpp, lua_names.cpp   steps after all translation units (no AST)
src/model.hpp/.cpp        the data model and its JSON
tests/                    fixtures, golden files, RunGolden.cmake
tools/                    parity.py, check_schema.py, dev-env.cmd, analyzer pins
```
