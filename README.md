# Description

This project strives to provide better typespec files and HTML documentation output for Overlunky than 
what currently exists in Playlunky under `submodules/overlunky/docs/` by utilizing Clang parsing facilities.

`docs-digger` parses overlunky's sources with Clang LibTooling, using overlunky's own `compile_commands.json`,
and extracts its Lua API. The work plan is in `.agents/01-plan.md`.

## Development Machine

There are multiple tools like MS Visual C++, CMake, Git, Python and Clang available globally on the development machine.
If they are not, read below `Dependencies Install`...

## Dependencies Install

### Windows (Development)

```powershell
winget install -e --id Kitware.CMake
winget install -e --id Ninja-build.Ninja
winget install -e --id Git.Git
winget install -e --id Python.Python.3.12
# clang-cl, lld-link: the scripts compile with C:\Program Files\LLVM\bin (same major as the pinned LLVM, 23)
winget install -e --id LLVM.LLVM
# MSVC STL + Windows SDK (skip if Visual Studio already has "Desktop development with C++")
winget install -e --id Microsoft.VisualStudio.2022.BuildTools --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

Clang/LLVM development libraries are not available from winget. The first configure downloads
the pinned `clang+llvm-<version>-x86_64-pc-windows-msvc.tar.xz` release (~860 MB, SHA256-checked)
and unpacks it into `thirdparty/` (git-ignored). Later configures reuse it. To use another
LLVM install, pass `-DClang_DIR=<llvm root>/lib/cmake/clang`.

Everything else that is downloaded (zlib and zstd for LLVM, Lua headers for sol2, the analyzer venv) also goes
to `thirdparty/`, pinned by version and SHA256. `build/` only holds output that a configure and build recreate.

An overlunky checkout with submodules is expected at `../Playlunky/submodules/overlunky`
(`git submodule update --init --recursive` in Playlunky). Elsewhere, set the environment variable
`DD_OVERLUNKY_ROOT` to the checkout before the first build. `dig.bat` and the tests both use it; the tests need
overlunky's sol2 headers.

### Linux

Not set up yet. Clang is expected to come from distro packages (`llvm-<ver>-dev`, `libclang-<ver>-dev`)
or a Docker image; pass `-DClang_DIR=...` if CMake does not find it on its own. overlunky itself only parses on
Windows (MSVC headers, Windows SDK).

## Build, Run, Test

The scripts enter the Visual Studio environment themselves; run them from any shell.

- `build.bat [Debug|Release|All]`: builds `docs-digger` with clang-cl and Ninja Multi-Config in `build\digger`.
- `test.bat [Debug|Release|All]`: builds and runs the CTest suite (golden-file tests).
- `dig.bat [options]`: builds Release, configures overlunky into `build\overlunky` for its `compile_commands.json`
  (configure only) and runs the digger on `src/game_api/**` (the events are triggered outside `script/`),
  adding the enum values of `docs/game_data/lua_enums.txt` (`--runtime-enums`) and keeping each translation unit's
  result in `build\digger-cache.sqlite` (`--cache`).
- `quality.bat [changed|all]`: the clang-format, Lizard and clang-tidy gate.

`dig.bat` passes its arguments to `docs-digger`, for example `dig.bat -o api.json --jobs 8`. The output is
JSON: `functions`, `values`, `usertypes`, `enums`, `aliases`, `libraries`, `script_callbacks`,
`global_variables` and `events`, each with its source location, documentation and C++ types; parameters that
receive a Lua function carry a `callback` signature (see `.agents/01-plan.md`, sections 4 and 5). Run
`build\digger\Release\docs-digger.exe --help` for all options.

The 101 `game_api` translation units are parsed in parallel (`--jobs`, default all hardware threads as LLVM counts
them; a sol2 unit takes up to 2 GB): about 30 s on 32 threads, about 7 minutes with `--jobs 1`. A unit whose files
(the source and everything it includes, compared by content) are unchanged since the last run comes from the cache,
so a run without source changes takes a few seconds; a new digger build, another `--root` or another compile command
parse again. `--no-cache` parses everything (and refreshes the cache); deleting `build\` only costs one full run.
The digger prints a line per finished unit and, every 10 s, the units still being parsed.

`python tools\parity.py <overlunky root> api.json` runs overlunky's old `docs/parse_source.py` and compares its
results with the digger's. It lists accepted differences with their reasons and exits with 1 on anything
unexplained. Its callback report counts callback signatures per origin and lists the unresolved ones and the
conflicts between the code and the `The callback signature is ...` / `Params:` docs (doc fixes for overlunky).

Golden tests compare the output for each `tests/fixtures/<name>.cpp` with `tests/expected/<name>.json`; a
`tests/fixtures/<name>.lua_enums.txt` next to a fixture is passed as `--runtime-enums`. After an
intended output change, run `set DD_UPDATE_GOLDEN=1` and then `test.bat`, and review the diff of `tests/expected/`.
