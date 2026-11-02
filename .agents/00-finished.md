# docs-digger: finished steps

Steps moved here from `01-plan.md` once their definition of done was met. The discoveries they made stay in the
plan (sections 2, 3 and 7), where later steps read them.

- **DIG-0. Toolchain and skeleton** (`4b2dfba`): `docs-digger` builds on the pinned LLVM 23.1.2 package (`/MT`,
  no RTTI, 10 MB stack, zlib/zstd from pinned sources) with `build.bat`, `test.bat`, `dig.bat` and the
  `quality.bat` gate. Overlunky's compile database comes from a configure only (pinned Lua via `LUA_LOCAL_DIR`,
  PCH flags dropped). Own CLI instead of `CommonOptionsParser`. Two Clang 23 workarounds are always on: delayed
  template parsing for sol2, the new constant interpreter for fmt. All 50 script TUs parse with 0 errors in
  154 s, sequentially. The skeleton B1 matcher prints 3183 key paths, and two runs give byte-identical output.
  413 of 415 old function names are found; `dump` and `get_entity` are doc-only comments (moved to DIG-1).
  Golden-file tests run through CTest (`key_paths` fixture), and a fresh copy builds and passes them.
- **DIG-1. Functions (B1; P1–P4)**: JSON output (`functions`, `values`) from `src/model.cpp`; `src/types.cpp`
  classifies every type (typedef names kept, member pointers as their field, arrays); `src/comments.cpp` attaches
  the `///` block directly above a binding, turns leading `NoDoc` / `Deprecated` into flags, and reads declaration
  comments of bound functions. `resolveBound()` covers lambdas, function names, `&f`, casts, `sol::overload`,
  `sol::resolve`, local variables, aliases (`lua["a"] = lua["b"]`), Lua-defined functions (`safe_script`) and
  environment roots; 28 commented-out bindings become `doc_only` entries. `tools/parity.py` checks all 415 old
  functions: 0 unexplained differences in presence, deprecation, parameter counts and doc text (1 accepted:
  `create_fmod_guid_map`, where the old parser merges a neighbouring comment), 0 unclassified types. Two runs give
  byte-identical output (50 TUs, 158 s). Fixtures: `functions`, `docs`, `key_paths`. `sol::property` /
  `sol::readonly` / `sol::var` moved to DIG-2 (usertype-only in overlunky).
