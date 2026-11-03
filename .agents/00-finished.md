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
- **DIG-1. Functions (B1; P1–P4)** (`feb873e`): JSON output (`functions`, `values`) from `src/model.cpp`; `src/types.cpp`
  classifies every type (typedef names kept, member pointers as their field, arrays); `src/comments.cpp` attaches
  the `///` block directly above a binding, turns leading `NoDoc` / `Deprecated` into flags, and reads declaration
  comments of bound functions. `resolveBound()` covers lambdas, function names, `&f`, casts, `sol::overload`,
  `sol::resolve`, local variables, aliases (`lua["a"] = lua["b"]`), Lua-defined functions (`safe_script`) and
  environment roots; 28 commented-out bindings become `doc_only` entries. `tools/parity.py` checks all 415 old
  functions: 0 unexplained differences in presence, deprecation, parameter counts and doc text (1 accepted:
  `create_fmod_guid_map`, where the old parser merges a neighbouring comment), 0 unclassified types. Two runs give
  byte-identical output (50 TUs, 158 s). Fixtures: `functions`, `docs`, `key_paths`. `sol::property` /
  `sol::readonly` / `sol::var` moved to DIG-2 (usertype-only in overlunky).
- **DIG-2. Usertypes (B2; P5, P6)** (`797461c`): `usertypes` in the JSON output (`src/usertypes.cpp`, shared AST helpers
  moved to `src/resolve.cpp`). `new_usertype<T>(...)` argument pairs and later `ut["k"] = v` (routed by the
  `sol::usertype<T>` root type) become members of kind field, readonly, property (getter/setter), method (with
  overloads and `self` kind) or value; `lua["Name"]["k"] = v` is moved into the usertype after all TUs.
  Constructors (`sol::constructors`, `sol::initializers`, `no_constructor`), bases (`sol::bases` = `base_list`),
  meta functions. `definitionOf()` follows `std::move`, locals and small factories, so
  `MakeParticleMemberAccess` and `self_return` need no special case. Member docs come from the declaration
  (unions and anonymous structs included), the field a getter reads, autodoc hint comments and same-name members.
  `tools/parity.py` checks all 527 old usertypes and 3488 members (names, kinds, docs, constructors, bases):
  0 unexplained, 79 accepted differences (mostly old "value" kinds the digger resolves to methods, old constructor
  lists that include unexposed C++ constructors, block-commented code), 0 unclassified types. 529 usertypes with
  3486 members, two runs byte-identical. `ut.set`, `sol::call_constructor` and `readonly_property` are unused in
  overlunky and not handled. Fixture: `usertypes`.
- **DIG-3. Enums, aliases, libraries, global variables, casts, events (B3, B4; P8–P13)**: new output
  sections `enums`, `aliases`, `libraries`, `script_callbacks`, `global_variables`; steps after all TUs
  in `src/merge.cpp` (no AST). Enums from `create_named_table` and `new_enum` (FMOD_* tables, missed by
  the old parser), values with source text and constant number or string, later `lua["E"]["K"] = v`
  keys, commented-out example pairs (`doc_comment`), `filled_at_runtime` for computed keys, docs from
  the ported `/* NAME // KEY // doc */` blocks (also for usertype members), and `--runtime-enums`
  (`lua_enums.txt`, passed by `dig.bat`): 92 enums, 4625 values. Aliases are typedefs under the root
  that some binding type names (43). Libraries per `open_libraries` call with its function. Global
  variables are the documented values on the state, no hardcoded list: the old `not_functions` plus
  `pause`. P8: the old "events" are documented reads of script globals (`on_frame`, 9 callbacks).
  `docBefore()` now takes only `///` comments. `tools/parity.py` checks `ps.enums` (values and docs),
  `ps.aliases`, `ps.lualibs`, `ps.known_casts`, `ps.events` and `not_functions`: 0 unexplained,
  88 accepted, 0 unclassified types; two runs byte-identical. Fixture: `enums` (with
  `enums.lua_enums.txt`).
- **DIG-4. Callback signatures (C1–C7; P7, P15)**: every parameter that receives a Lua function carries a
  `callback` (params with front/back binders marked, returns, `nil_allowed`, `nil_meaning`, origin, `stored_in`,
  `selector`, `prose`, `conflicts`); new output section `events`. `src/callbacks.cpp` follows each
  `sol::function` param through moves, locals and captures to `make_safe_cb` (binders), `handle_function`, a
  direct call, a record field (also per container), a C++ function (followed into its body) or a Lua function
  (callback copied); dispatch sites are keyed by enum guards, enum params followed back through callers, and
  container writes; C6 reads `.value_or(X)`, condition and return consumers. C1 (`std::function`) and the joins
  run in `src/callback_join.cpp`. C5 builds `set_pre_`/`set_post_`/`*_virtual` members from the
  `HookableVTable` variables (`LiteralString` keys, `/// NoDoc` from the written alias, hook nil rules). C7 ports
  `reSignature` (prose fills unresolved callbacks) and checks `Params:` lines of `ON` events. Default `--select`
  widened to all 101 `game_api` TUs (0 errors, about 7 minutes); progress line per TU and a 10 s heartbeat.
  Overlunky: 358 callback params (287 vtable hooks, 30 unresolved: `set_*_virtual` choose the hook at runtime,
  two functions defined in other TUs), 247 events (211 from sites, 30 generic screen events, 6 never dispatched
  `THEME_OVERRIDE` keys); every `ON` event resolved. P7 hook names and NoDoc match the old parser.
  `tools/parity.py`: 0 unexplained, 88 accepted, 0 unclassified types, callback report with 8 conflicts handed
  over as overlunky doc fixes (`set_on_damage`, `set_on_destroy`, `set_on_kill` x2, `set_post_render`,
  `set_vanilla_sound_callback`, `ON.PRE_GET_FEAT`, `ON.PRE_SET_FEAT`); two runs byte-identical. Fixtures:
  `callbacks`, `vtables`.
