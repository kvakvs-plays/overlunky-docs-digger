# docs-digger: work plan

Build a new standalone CMake C++ tool, `docs-digger`. It gets the overlunky source root (and its
`compile_commands.json`) as an input path. It parses the overlunky sources with Clang and extracts
the Lua API into a structured data file. It replaces the regex-based extraction in
`docs/parse_source.py`: it must find at least everything that file finds, and add the details the
regexes can't see, mainly:

- exact parameter and return types, including nullable/optional parameters and `nil` returns;
- signatures of Lua callbacks (handler functions): what arguments they get and what they may return,
  including the common "return nothing (`nil`)" case and what the C++ side does when `nil` comes
  back.

- Old docs parser location for reference: `..\Playlunky\submodules\overlunky\docs\`
- Source files root is `..\Playlunky\`

DIG-0 to DIG-5 cover **data extraction**: the extracted data has to carry every detail the current
generator has, plus the new ones. DIG-6 and DIG-7 turn it into the two outputs that replace
`generate_emmylua.py` and `generate.py` + Slate: LuaLS definition files and an HTML reference with search.

- Guiding rule: **write as little code as possible.** Use Clang LibTooling, AST matchers and LLVM's
  support libraries for everything they can do. Write custom code only for the sol2-specific binding
  patterns.
- When implementing steps, if the scope is too broad, add substeps to the plan.
- Commit with message prefix "[plan]" and have full plan title in the message
- Move finished plan steps into 00-finished.md and compact them add as a bullet point



## 1. What the current generator extracts (parity checklist)

Taken from `docs/parse_source.py` and `docs/generate.py`. Every item must be present in the new
data.

- **P1** Global functions `lua["name"] = ...` (lambda, function reference, `sol::overload`) with
  param names, param types, return type, `///` comment. Found today by: regex over `api_files`.
- **P2** Namespaced/member functions `lua["Type"]["name"] = ...`, resolved through the `rpc` list
  from headers. Found today by: regex plus header-signature lookup.
- **P3** Deprecated functions (`/// Deprecated`, `deprecated_func.cpp`). Found today by: comment
  directive.
- **P4** `/// NoDoc` items are hidden; directives `Returns`, `Default`, `note:`, `Warning:` are kept
  in the text. Found today by: substring checks.
- **P5** Usertypes `new_usertype<T>("Name", "key", value, ...)`: fields, methods, `sol::property`
  getters and setters, readonly, overloads, constructors, `sol::bases<...>`,
  `MakeParticleMemberAccess`. Found today by: regex plus bracket counting.
- **P6** C++ class/struct members (fields and methods, including those inside unions and anonymous
  structs) with their `///` comments; used to document usertype members. Found today by: line-based
  class scanner over `header_files`.
- **P7** Vtable hooks: `VTableEntry<"name", idx, MemFun<&T::f> or Sig, BackBinder<...>>` become
  `set_pre_<name>` / `set_post_<name>` per usertype; `/// NoDoc` per entry. Found today by: regex in
  `vtables_lua.cpp`, `theme_vtable_lua.hpp`.
- **P8** Events / doc-only statements `lua["NAME"];` with comments. Found today by: regex.
- **P9** Enums: `create_named_table("NAME", "K", v, ...)`, `/* ... */` value-list comments, plus
  runtime-filled enums from `docs/game_data/lua_enums.txt`. Found today by: regex plus text file.
- **P10** Entity casts `lua["Entity"]["as_*"]`. Found today by: regex.
- **P11** Lua libraries from `open_libraries(...)`. Found today by: regex.
- **P12** Aliases `using X = Y;`. Found today by: regex.
- **P13** Global variables (`players`, `state`, `game_manager`, `online`, `savegame`, `options`,
  `meta`, `prng`). Found today by: hardcoded `not_functions` list.
- **P14** Function groups ("Message functions", "Generic functions", ...). Found today by: hardcoded
  name-substring table in `generate.py` (`func_cats`).
- **P15** Callback signatures. Found today by: **only** parsed from comment prose:
  `The callback signature is <ret> <name>(<params>)` (`reSignature`).
- **P16** C++ to Lua type renaming (`replace_table`). Found today by: string replace.

P16 is a presentation concern. The digger emits C++ types plus structured type facts (see section
4), and the Lua naming is applied at the output stage, reusing the existing `replace_table` rules.



## 2. Technology choice

**Clang LibTooling + ASTMatchers (C++ API), with output written using `llvm::json`.**

- `ClangTool` reads `compile_commands.json` and handles include paths and defines. We write no
  build-system glue. The CLI uses own `llvm::cl` options (`-p`, sources, `--extra-arg`, `--` for a
  fixed database) instead of `CommonOptionsParser`: that one loads the `-p` database only when
  source paths are listed, and parsing a selection of the database without listing files is the
  normal use (found in DIG-0).
- `ast_matchers::MatchFinder` lets each extraction pass be a declarative matcher plus a short
  callback. This is far less code than walking cursors with the libclang C API.
- The C API (`libclang`, which is what the local `C:\Program Files\LLVM` 23.1.2 install has) is
  **rejected**. It can't conveniently reach template arguments of call expressions
  (`handle_function<Ret>`, `make_safe_cb<Sig>`), the call operators of lambdas, or
  overloaded-operator chains (`lua["a"]["b"] = x`). Those are exactly the parts we need.
- Comments: run with `-fparse-all-comments`.
  - For declarations: `ASTContext::getRawCommentForDeclNoCache`.
  - For binding statements (comments are attached to `lua[...] = ...` statements, not to
    declarations): `ASTContext::Comments.getCommentsInFile(FileID)`, taking the comment block that
    ends directly before the statement. `RawComment::getFormattedLines` strips the `///` prefixes.
- Constant values (enum values, VTable indices): `Expr::EvaluateAsInt` / `EvaluateAsRValue`.
- Type facts: `QualType` + `PrintingPolicy` for spelling, `getCanonicalType()` for resolution, and
  `ClassTemplateSpecializationDecl::getTemplateArgs()` to unwrap `std::optional<T>`, containers,
  `std::function<Sig>` and so on.
- Parallelism over translation units (DIG-4b): one `ClangTool` per TU on an `llvm::DefaultThreadPool`, each with
  its own passes and partial `Extraction`, merged in source order. `AllTUsToolExecutor` is not used: its results
  are key/value strings, while each TU's partial extraction must also be cached on its own.
- Result cache (DIG-4b): SQLite (pinned amalgamation; LLVM has no database), one row per TU with the content hash
  of every file it read; `src/codec.cpp` writes the partial extraction losslessly with one generic encoder and a
  field list per model struct.
- Output: `llvm::json::OStream` / `llvm::json::Value`. No third-party JSON dependency.
- Tests: golden-file comparison through CTest (`cmake -E compare_files`). No lit/FileCheck setup.

### Toolchain acquisition

- Pin **one** LLVM major version (proposal: 23.x, matching the local install). The Clang C++ API is
  not stable across majors.
- The local `C:\Program Files\LLVM` installer ships no Clang C++ libraries and no
  `ClangConfig.cmake`, so it can't be used. Use the official
  `clang+llvm-<ver>-x86_64-pc-windows-msvc.tar.xz` release archive, which ships the static libraries
  and `lib/cmake/{llvm,clang}`. Point CMake at it with `-DClang_DIR=<root>/lib/cmake/clang`.
  Verified in DIG-0; what the package needs from us:
  - it is Release-only with the static runtime (`/MT`) and no RTTI: the digger uses `/MT` in every
    configuration and `/GR-`;
  - its LLVMSupport links zlib and zstd, which it does not ship: `cmake/Compression.cmake` builds
    both from pinned sources in `thirdparty/`;
  - Clang's builtin headers are found through `-resource-dir=<package>/lib/clang/<major>`;
  - deep sol2 template instantiation needs a 10 MB stack (`/STACK`, as LLVM's own tools).

### Input: compilation database

- Overlunky is Windows-only (Windows SDK, Detours, MSVC STL). The digger must therefore run on
  **Windows**, with clang-cl driver semantics and the MSVC headers available.
- Produce `compile_commands.json` by configuring overlunky with
  `cmake -G Ninja -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`
  (`cmake/OverlunkyDatabase.cmake`, run by `dig.bat`). Only configure; verified in DIG-0:
  - sol2 would download the Lua sources at build time; `LUA_LOCAL_DIR` points it at the pinned copy
    in `thirdparty/` instead, so no build is needed and nothing is downloaded into `build/`;
  - `spel2_api` uses a precompiled header; the digger drops the `/Yu` and `/Fp` flags and keeps the
    `/FI` of the generated `cmake_pch.hxx`, so every TU still sees the same headers;
  - overlunky is built with Clang 17, and two dependencies fail with Clang 23. The digger always
    adds `-fdelayed-template-parsing` (sol2 3.3.0 `optional<T&>::emplace` names a missing member)
    and `-fexperimental-new-constant-interpreter` (fmt's `FMT_STRING` pointer arithmetic on string
    literals, rejected by the classic evaluator since CWG2765). With both, all 50 script TUs parse
    with 0 errors.
- Process every translation unit under `src/game_api/script/**` (and `src/game_api/*.cpp` where
  needed) instead of the hand-maintained `header_files`/`api_files` lists. Headers are reached
  through includes. Results are filtered to declarations and statements located under
  `src/game_api/`.
- **CI consequence:** `build-docs.yml` runs on `ubuntu-latest`. Decided in DIG-5: the `windows-latest`
  job lives in this repository (`.github/workflows/dig.yml`), next to the digger and its pins, so
  overlunky doesn't have to build it. It checks out overlunky (`spelunky-fyi/overlunky` `main` by
  default, repository and ref are `workflow_dispatch` inputs), fetches the LLVM package first
  (`cmake -P cmake/LLVMPackage.cmake`: a configure needs a compiler before it can download LLVM) and
  compiles with the package's own clang-cl (`DD_LLVM_BIN`; verified locally), runs the tests, digs,
  checks the schema and uploads the artifacts `docs-digger-api` and `parity-report` (informative,
  `continue-on-error`). The Slate job takes the JSON with `gh run download` (README).



## 3. Project layout

```
F:\Projects\Spelunky\docs-digger\   (separate from overlunky; takes the overlunky root as an argument)
  CMakeLists.txt          standalone; downloads pinned Clang/LLVM into thirdparty/ on Windows
  thirdparty/             git-ignored; every download (LLVM, zlib, zstd, Lua, analyzer venv)
  README.md               how to get LLVM, generate compile_commands.json, run
  build.bat, test.bat     build / build + CTest (clang-cl, Ninja Multi-Config, build/digger)
  dig.bat                 overlunky compile database (build/overlunky) + run the digger on it
  quality.bat             clang-format, Lizard, clang-tidy gate
  cmake/                  downloads (LLVM pin in LLVMPackage.cmake), zlib/zstd/SQLite builds, overlunky configure, quality checks
  config/groups.json      function-group rules ported from generate.py func_cats (P14)
  schema/                 docs-digger.schema.json: the frozen output format, every field described (DIG-5)
  .github/workflows/      dig.yml: Windows CI that builds, tests, digs overlunky and uploads the JSON (DIG-5)
  src/main.cpp            CLI, merge steps, write output
  src/runner.cpp          TUs in parallel, each into its own Extraction (or from the cache); progress, ordered merge
  src/cache.cpp           SQLite result cache, content hashes of files
  src/codec.cpp           lossless JSON of a partial Extraction (field lists per model struct)
  src/model.hpp/.cpp      plain structs for the data model + toJSON() overloads (llvm::json)
  src/source_root.cpp     --root: which files results may come from, root-relative paths
  src/types.cpp           QualType -> TypeInfo (section 4)
  src/comments.cpp        comment lookup for decls/statements, directive parsing
  src/bindings.cpp        AST matchers, B1 (lua[...] = ...), B3 (enum tables, typedefs), B4, script callback reads
  src/merge.cpp           steps after all TUs, no AST: runtime enum file, block docs, used aliases, global variables
  src/groups.cpp          step after all TUs, no AST: function groups from the directive, owner and --groups rules (P14)
  src/resolve.cpp         sol2 expression helpers shared by the passes: key paths, definitionOf, resolveBound
  src/usertypes.cpp       B2: new_usertype arguments, members, constructors, bases, member docs
  src/callbacks.cpp       passes C2-C6 and the AST part of C5 (parameter flows, dispatch sites, keys, vtable hooks)
  src/callback_join.cpp   callback steps after all TUs, no AST: C1, joins, events, vtable members, C7 prose
  src/lua_names.cpp       DIG-6: Lua spelling of every TypeInfo (P16, rules in config/lua_types.json)
  src/emit_lua.cpp        DIG-6: LuaLS definition files (---@meta), split under the size limit
  src/html/starlight/     DIG-7: Astro Starlight project that renders the JSON; site.bat builds it.
                          Self-contained, so another generator can sit next to it (src/html/<name>/)
  tests/fixtures/*.cpp    small sources using the real sol2 headers from <overlunky>/src/sol2
  tests/expected/*        golden outputs (*.txt key-path lists in DIG-0, *.json from DIG-1)
  tests/RunGolden.cmake   runs one fixture and compares; DD_UPDATE_GOLDEN=1 rewrites the golden file
  tools/parity.py         loads <overlunky>/docs/parse_source.py results, diffs them with digger JSON (reuses the old parser as the oracle)
  tools/check_schema.py   checks outputs against the schema (standard library only; CTest schema.<name>, CI)
```

Expected size: about 1,500 to 2,000 lines of C++ in total. Most of it goes to sol2 pattern
resolution and callback discovery.



## 4. Data model (content, not the final format)

Every entity carries `source {file, line}` and `doc {lines[], directives{NoDoc, Deprecated, Returns,
Default, Group, ...}}`.

- **TypeInfo**: `spelling` (as written, typedef names kept, also for inner types), `canonical`,
  `kind`
  (`void | bool | int | float | string | enum | record | pointer | optional | container | tuple | function | lua_object | variadic | nil | unknown`),
  `inner[]` (container elements, optional payload, tuple members, `std::function` parameters),
  `returns[]` (of a `std::function`), `nullable` (std/sol `optional<T>`, raw pointer that sol maps
  `nil` to `nullptr`, `sol::object`), and `hidden` (a sol2 injected parameter such as
  `sol::this_state` or `sol::this_environment`, which is not visible from Lua). A member pointer
  (`&T::field`) is classified as the member's type.
- **Param**: `name`, `type: TypeInfo`, `omittable` (trailing nullable params),
  `callback: CallbackSig?` (DIG-4). Nullable and variadic are read from the type (`nullable`, kind
  `variadic`), not repeated on the param.
- **FunctionSig**: `params[]`, `returns: TypeInfo[]` (a tuple/pair becomes multiple returns;
  `optional` means it may return `nil`; `void` means it returns nothing), `cpp` (qualified name of
  the bound C++ function or `lambda at <file>:<line>`), `doc` (the C++ declaration's comment, for
  named functions), `self` (usertype members: `implicit` for C++ member functions, `first_param`
  when the first parameter is the object).
- **Function**: `name`, `owner` (keys of the namespace table or usertype; empty for globals), `root`
  (source text of the root when it is neither the state nor an environment), `group` (DIG-5),
  `overloads[]: FunctionSig`, `deprecated`, `nodoc`, `doc_only` (from a commented-out binding),
  `defined_in_lua` (`lua.safe_script(...)`), `alias_of` (`lua["a"] = lua["b"]`; overloads copied
  from the target).
- **Value**: a binding whose right side is not callable: `name`, `owner`, `root`, `type`, `doc`,
  `doc_only`.
- **Usertype**: `name`, `cpp`, `doc` (above the `new_usertype` statement), `cpp_doc` (of the C++
  class), `bases[]` (C++ names from `sol::bases<...>`), `constructors[]: FunctionSig`,
  `no_constructor`, `members[]`.
- **Member**: `name`, `kind` (`field | readonly | property | method | value`; an overload set is a
  method with several overloads), `meta` (keyed by a `sol::meta_function`), `type` (field type,
  property type from the getter, or value type), `overloads[]` (methods), `getter` / `setter`
  (properties, present when bound), `doc` (above the binding), `cpp_doc` (of the bound C++ field,
  the field a getter reads, an autodoc hint, or the C++ member of the same name).
- **CallbackSig**: `params[]` (with front/back binder params marked), `returns`, `nil_allowed`,
  `nil_meaning` (for example "treated as false", "original call proceeds"), `origin`
  (`std_function | make_safe_cb | handle_function | direct_call | stored_handle_function | event_dispatch |
  vtable_hook | doc_comment | unresolved`), `stored_in` (`Record::field`, `in Container` when known),
  `selector` (enum whose value selects the signature; then the signatures are in `events`), `unresolved` (the
  pattern that stopped the search), `prose`, `conflicts[]` (between dispatch sites, or with the prose comment),
  `source` (the site it was read from).
- **Enum**: `name`, `doc`, `values[] {name, text, value? (number or string), doc, origin (static |
  doc_comment | runtime_file)}`, `filled_at_runtime` (code adds keys in a loop); a table only the
  runtime file knows has `origin: runtime_file` and no source.
- **Event**: `table` (`ON`, `THEME_OVERRIDE`), `name`, `stored_in`, `callback: CallbackSig`, `generic` (the
  key has no site of its own; the generic dispatch's signature), `doc` (of the enum value) (DIG-4).
- **ScriptCallback** (P8): a documented global the script defines and C++ reads (`on_frame`):
  `name`, `type`, `doc`.
- **Alias** (`name`, `type`, `doc`), **LibraryCall** (`names[]`, enclosing `function`),
  `global_variables` (names of the documented values on the state; their types are in `values`).



## 5. Extraction passes

### Binding passes

**B1. Lua table assignments (P1, P2, P8, P10, P13).** Matcher:
`cxxOperatorCallExpr(hasOverloadedOperatorName("="))` whose LHS is a chain of `operator[]` with
string-literal keys on `sol::state`, `sol::state_view`, `sol::table` or `sol::usertype`. Collect the
key path, then resolve the RHS through one shared `resolveBound(Expr*)` helper:

- `LambdaExpr` → `getCallOperator()`.
- `DeclRefExpr` / `UnaryOperator(&)` → `FunctionDecl` / `CXXMethodDecl` / `FieldDecl`.
- `static_cast<Sig>(&f)` → the selected overload (Clang has already resolved it).
- `sol::overload(a, b, ...)` → resolve each argument.
- `sol::property(get[, set])`, `sol::readonly(&T::f)`, `sol::var(...)`.
- Anything else is a value, so it's recorded as a global variable or constant with its type. This
  replaces the `not_functions` list.

Found in DIG-1 and handled there:

- a local variable binds what it was initialized with
  (`auto spawn_tree = sol::overload(...); lua["spawn_tree"] = spawn_tree;`);
- `lua["a"] = lua["b"]` is an alias; the overloads of `b` are copied after all TUs are parsed;
- `lua["printf"] = lua.safe_script(...)` is a function written in Lua (`defined_in_lua`, no C++
  signature);
- a `sol::environment` root names globals like the state does (`LuaBackend::lua`: `on_frame`,
  `meta`, ...);
- a `///` block followed by a commented-out binding
  (`// lua["get_entity"] = [](uint32_t uid) -> Entity*{};`) documents a function written in Lua, or
  the signature of an overload set bound right below it. Lambda text becomes a `doc_only` function
  with text-only types; other text (`lua.create_named_table("options")`) becomes a `doc_only` value.
  28 such entries in overlunky;
- `sol::property`, `sol::readonly` and `sol::var` only appear on usertype objects, so they moved to
  B2 (DIG-2).

Expression statements of the form `lua["NAME"];` (P8) are matched separately and only carry docs
(none exist in overlunky; see DIG-3 below B4).

**B2. Usertypes (P5, P6).**

- `new_usertype<T>(name, k, v, k, v, ...)`: walk the argument pairs and feed each value to
  `resolveBound`.
- Special keys: `sol::base_classes` / `sol::bases<...>`, `sol::meta_function::*`,
  `sol::constructors<...>`, `sol::no_constructor`, `sol::call_constructor`.
- Later assignments on the returned usertype object (`ut["k"] = v`, `ut.set(k, v)`) go through B1.
- Docs for `&T::member` come from the `FieldDecl`/`CXXMethodDecl` comment, which replaces the
  line-based class scanner. Unions and anonymous structs need nothing special in Clang.
- `MakeParticleMemberAccess<...>()` is handled as one small special case.

Found in DIG-2 and handled there (`src/usertypes.cpp`, shared helpers in `src/resolve.cpp`):

- most members come from `xxx_type["k"] = v` on the returned `sol::usertype<T>`, routed to T by the
  root's type; `lua["Name"]["k"] = v` bindings are moved into the usertype named Name after all TUs
  (that covers the `lua["Entity"]["as_*"]` casts, P10);
- `definitionOf()` follows `std::move(x)`, local variables and small factories (a body of type
  declarations and static_asserts plus one `return`), which covers
  `MakeParticleMemberAccess<&P::x>()` and `self_return<&T::f>()` without special cases; the
  factory's member template argument documents the result;
- constructors come either first (`sol::constructors<...>{}`, `sol::no_constructor`) or as `"new",
  sol::initializers(...)`; listed signatures are matched with declared constructors for names and
  comments;
- `sol::bases` is an alias of `sol::base_list`, and `sol::base_classes` is a `base_list<>` value;
- docs the old parser finds by text, rebuilt from the AST: the field a property getter reads
  (`s.leader`), autodoc hints (`// &Vec2::split); // for the autodoc` above a value,
  `// ut["pause"] = &PauseAPI::pause;` above a `/// NoDoc` binding; a hint wins over other
  comments), and the C++ member of the same name as a last resort;
- not reproduced, on purpose: members bound inside `/* ... */` block comments (CustomTheme in
  `level_lua.cpp`), and docs written on fake declarations inside `/* // just for the autodoc */`
  blocks (`Triangle`/`Quad` `split` and `is_point_inside` in `math.hpp`). Doc fixes for overlunky:
  move those comments onto the real declarations;
- overlunky never uses `ut.set(k, v)`, `sol::call_constructor` or `sol::readonly_property`, so they
  are not handled; `sol::var(v)` isn't used either but is covered (a `value` member) because it
  costs one comparison.

**B3. Enums (P9, P12).**

- `create_named_table(name, k, v, ...)` and `lua["ENUM"]["K"] = v`: values via `EvaluateAsInt`.
- `/* ... */` value-list comments: port the existing logic if any are still needed after the static
  values are in; check in DIG-3.
- Runtime enums (ENT_TYPE and so on) still come from `docs/game_data/lua_enums.txt` and are merged
  with `origin=runtime_file`.
- Aliases: `TypeAliasDecl` in `src/game_api`, keeping only those referenced by some Lua-visible
  type.

**B4. Libraries (P11).** Arguments of `open_libraries(...)`.

Found in DIG-3 and handled there (matchers in `src/bindings.cpp`, steps after all TUs in
`src/merge.cpp`, which reads no AST):

- `lua.new_enum(name, k, v, ...)` creates enums too (the `FMOD_*` tables, missed by the old
  parser); its name is a `string_view` parameter, so string literals are read through converting
  constructors;
- an enum value keeps its source text (`ON::LOGO`, `g_level_max_y`) and its number when it is a
  constant; `lua["KEY"]["OL_MOD_CTRL"] = 0x100` adds static values the old parser never saw. The
  enum must be created earlier in the same TU (true in overlunky); matchers run in source order;
- runtime-filled tables write example pairs as comments inside the call
  (`create_named_table("ENT_TYPE" //, "FLOOR_BORDERTILE", 1`); they are read from the source lines
  of the call (`origin: doc_comment`), and a computed key (`lua["ENT_TYPE"][name] = ...`) sets
  `filled_at_runtime`. An empty named table holding only functions (`Util`, `TYPE_MAP`) is a
  namespace and is dropped;
- the `/* NAME // KEY // doc */` blocks are still needed (static values carry no docs): ported for
  16 enums and 6 usertypes. A key named twice keeps its last docs, as before;
- `docBefore()` now takes only `///` comments: a `/**/` at the end of the previous line (ImGuiIO)
  is no doc;
- aliases: typedef and `using` names declared under the root at namespace scope, kept when an
  identifier in some binding's type spelling names them (also through another kept alias); alias
  templates are skipped;
- global variables (P13): the documented values on the state, plus the doc of a bound local
  variable (`meta`). They equal the old `not_functions` list, plus `pause` (missed there);
- P8: overlunky has no `lua["NAME"];` statements; the old "events" are documented reads
  (`sol::optional<sol::function> on_frame = lua["on_frame"];`), recorded as `script_callbacks`.
  The old regex also matches the right sides of alias bindings (`print`, `set_flag`).

### Callback passes

These are the new part. Each one fills `Param.callback` for any parameter that receives a Lua
function.

**C1. `std::function<R(A...)>` parameters.** The signature comes directly from the parameter type,
since sol2 converts Lua functions. No dataflow is needed.

**C2. `make_safe_cb<Sig>(cb, FrontBinder{...}, BackBinder{...})` and
`make_safe_clearable_cb<Sig>(...)`.** When `cb` is a `DeclRefExpr` to a bound function's
`sol::function` parameter:

- signature = `FrontBinder` argument types (for callables, their call operator return type) + `Sig`
  params + `BackBinder` argument types;
- return = return type of `Sig`.

`safe_cb.hpp` documents these semantics; encode them once.

**C3. Stored callbacks dispatched with `handle_function<Ret>(backend, x.F, args...)`.**

- In the binding lambda, find the record construction (`CXXConstructExpr` or `InitListExpr`) that
  the `sol::function` parameter flows into, and note the record type `R` and field `F`.
- Across all TUs, find `callExpr(callee(functionDecl(hasName("handle_function"))))` whose function
  argument is `memberExpr(member(F))` on an object of type `R`. Take `Ret` from the template
  argument and the argument types from the call.
- Several call sites are merged. If their arities or types differ, record a conflict.
- Template instantiations are traversed by `MatchFinder` by default, so the generic
  `handle_function<Ret>` sites resolve through their instantiations.

**C4. Events (`set_callback(cb, ON.X)`).** For the same `handle_function` matches, inside
`hasAncestor(caseStmt(hasCaseConstant(declRefExpr(to(enumConstantDecl())))))` on the `ON` enum, map
`ON::X` to the signature at that site. This produces per-event callback signatures (P8/P15) that
aren't documented today.

**C5. Vtable hooks (P7).**

- From `HookableVTable<Base, VTableEntry<"name", idx, SigOrMemFun, BackBinder<...>>...>` read the
  template arguments with the AST; the string literal is a template argument.
- Derive the `set_pre_<name>` and `set_post_<name>` signatures: self + params + back-binder params.
- Pre-hook return semantics ("return true to skip the original", `nil` = proceed) come from
  `hookable_vtable.hpp` and are encoded as one rule.

**C6. `nil` semantics.**

- `handle_function` always yields `optional`, so `nil` is always accepted.
- Record what the consumer does with it. Match `.value_or(X)` (record X), `has_value()` /
  `operator bool` branches ("nil = no override"), and `handle_function<void>` (return value
  ignored).
- For `make_safe_cb` with a non-optional `R`, mark the return as "required; nil converts by sol2
  rules". Exact semantics are to be confirmed by reading `safe_cb.inl`.

Rules read from `safe_cb.hpp` / `safe_cb.inl`, `handle_lua_function.inl` and `hookable_vtable.inl` in DIG-4
(encoded once in `src/callbacks.cpp`):

- binders: Lua gets `FrontBinder` values + the `Sig` params + `BackBinder` values. A bound value of type T is
  passed as is, unless T is invocable: then its call result is passed (`Binder<T>::get` calls it without
  arguments, or with the callback's arguments), so the param type is the call operator's return type;
- `handle_function<R>(backend, f, args...)` returns `optional<R>` (`bool` "call succeeded" for `R = void`):
  `nil`, no value, a Lua error and a failed conversion all give an empty optional. `R = std::monostate`
  ignores the result like `void`. Entity pointers in `args` are cast to their real Lua type;
- `make_safe_cb<R(A...)>` on a `sol::function`: `R = void` ignores the result, `optional<T>` passes an empty
  optional on, any other `R` turns `nil` into `R{}` (`false`, `0`, an empty value);
- `std::function<R(A...)>` params (C1) convert through sol2: `void` ignores the result, `bool` takes `nil` as
  `false`, nullable `R` takes it as empty; other `R` need a value;
- vtable hooks: a pre-hook of a `void` function returns `bool` through `make_safe_clearable_cb`, so `nil` is
  `false` ("the original runs"); `true` skips the original (the last pre-hook's answer counts). A pre-hook of
  an `R` function returns `optional<R>`: a value replaces the result and skips the original (the first value
  wins), `nil` lets the original run. Post-hooks return nothing. Hooks get the object first (cast to its real
  type), then the virtual's params, then the `BackBinder` values;
- consumers of `handle_function` (C6): `.value_or(X)` (also on a local `optional` read later) means "nil is
  X"; the optional used as a condition or `if (auto r = ...)` means "nil keeps the default behavior"; a
  returned optional is passed on.

Found in DIG-4 and handled there (`src/callbacks.cpp` reads the AST, the join runs in `src/merge.cpp`):

- a `sol::function` param is followed through `std::move`, locals and lambda captures (also init captures)
  to the first of: `make_safe_cb` (C2), `handle_function` on it (origin `handle_function`), a direct call
  `f(args)` (`direct_call`, `filter_entities`), a record it is stored in (`ScreenCallback{cb, ...}`,
  `x->func = f`; C3, keyed `Record::field`), a C++ function it is passed to (followed into that function's
  body when the TU has it), or a Lua function it is passed to (`lua["Entity"]["set_pre_x"](ent, fun)`, the
  deprecated entity callbacks: the target's callback is copied after all TUs). Facts are keyed by the
  function (qualified name, template arguments, parameter types; lambdas by location) and parameter index, so
  a declaration in a header and its definition in another TU meet; they are joined with the bound signatures
  after all TUs;
- one record type may be stored in several containers (`EntitySpawnCallback` in `pre_entity_spawn_callbacks`
  and `post_entity_spawn_callbacks`): a record added with `push_back`/`emplace_back` keeps its container, and a
  dispatch site keeps the container of the range-for around it (`stored_in: "Record::field in Container"`);
- dispatch sites are `handle_function` calls on a stored field. A site's key (C4) is the enum constant it is
  guarded by (`case ON::FRAME:`, `if (callback.screen == ON::LOAD)`, compared with a member of the same
  object), or an enum param of its function (compared that way, or used in the storage expression:
  `overrides[index]->func`). Enum params are followed back through calls (`pre_event(ON::PRE_UPDATE)` →
  `on_pre(event)`) and locals (`auto index = THEME_OVERRIDE::X; run_pre_func(index)`), across TUs. A site in a
  range-for over a container takes the constant that guarded the write into it (`load_callbacks[id] = cb`
  under `if (cb.screen == ON::LOAD)`). Unkeyed sites on a keyed storage are the generic dispatch (screen
  events) and serve the keys without a site of their own (`events[].generic`);
- events are the keys of a storage whose binding also takes an enum param of the key's type (`set_callback(cb,
  ON event)`, `CustomTheme:override(THEME_OVERRIDE, function)`): every value of the Lua table of that name gets
  an entry (`events`), with the signature of its sites, the generic one, or none (unresolved);
- the triggering TUs are outside `script/` (`state.cpp`, `level_api.cpp`, `render_api.cpp`): the default
  `--select` now covers all of `src/game_api/` (101 TUs);
- event prose (C7) is checked only for tables whose docs use the `Params:` convention (`ON`); `THEME_OVERRIDE`
  has none;
- vtable hooks (C5) come from the `HookableVTable<Self, Cb, Entries...>` variables (`static EntityVTable
  vt(lua, lua["Entity"], "ENTITY_OVERRIDE")`): the usertype is the key path of the second argument, entries
  and their `/// NoDoc` come from the written alias, signatures from the registration lambdas in the
  `register_hooks` instantiations (keys joined from the `LiteralString` template arguments). The generic B1
  records of those lambdas (root `lua_type`) are replaced by the usertype members.

**C7. Doc-prose fallback and lint.**

- Keep parsing `The callback signature is ...` (the `reSignature` regex, ported to `llvm::Regex`).
- If C1 to C6 produced nothing, use the prose (`origin=doc_comment`).
- Otherwise compare the two and emit `conflicts[]`. This gives a ready list of wrong or missing
  callback docs to fix in the sources.

### Grouping (P14)

`group` is chosen in this order:

1. an explicit `/// Group: <name>` directive (new and optional);
2. the rules in `config/groups.json`, ported from `generate.py` `func_cats`;
3. a fallback derived from the source file (`spawn_lua.cpp` → "spawn").

Members of usertypes and namespace tables are grouped by their owner.

Found in DIG-5 and handled there (`src/groups.cpp`, a step after all TUs):

- `generate.py` has its own catch-all, "Generic functions" (126 functions). It is ported as the
  `fallback` of `config/groups.json`, so every function keeps its old group; the source-file fallback
  only applies when the `--groups` file has no fallback (or there is none, as in most fixtures). Rules
  are tested in order with `name_contains`, `names` (whole names: `door`, `layer_door`) and
  `file_contains` (`game_patches_lua.cpp`), as `func_cats` does;
- namespace-table functions (`TYPE_MAP`, `io`, `os`) take their table (root and owner keys) as group,
  before the rules; a `/// Group:` line is read anywhere in the comment and taken out of the text
  (overlunky has none yet);
- `tools/parity.py` runs the `func_cats` loop of `generate.py` itself (cut out of its source and
  executed on `ps.funcs`, since the rest of `generate.py` writes the Markdown files): all 353 old
  functions get the same group (23 groups).



## 6. Steps

Work through the steps in order. Check items off (`[x]`) as they are finished. A step is finished
when its own definition of done and the common one below are both met. A finished step moves to
`00-finished.md` as one compact bullet; its discoveries stay in sections 2, 3 and 7. Finished so
far: DIG-0, DIG-1, DIG-2, DIG-3, DIG-4, DIG-4b.

**Definition of done for every step**

- [ ] Builds on Windows (Debug and Release) without warnings.
- [ ] `ctest` passes. Every pattern newly covered in the step has a fixture in `tests/fixtures/` and
  a golden file in `tests/expected/`.
- [ ] `quality.bat all` passes (clang-format, Lizard, clang-tidy), with no new suppressions or
  raised thresholds.
- [ ] From DIG-1 on: the `tools/parity.py` report is updated. Nothing the old parser finds is
  missing, unless it is listed in the report as an accepted difference with a reason.
- [ ] Running the digger twice on the same input gives byte-identical output.
- [ ] Discoveries that change the plan (new patterns, rejected approaches, timings) are written into
  this file.

### DIG-5. Grouping, finalization, CI (P14)

- [x] `config/groups.json` ported from `generate.py` `func_cats`.
- [x] Group selection order: `/// Group:` directive, then `config/groups.json`, then the source-file
  fallback; members grouped by their owner.
- [x] `tools/parity.py`: group of every function against `generate.py`.
- [x] Freeze the JSON schema: document every field (schema file or section in `README.md`) and add a
  `schema_version`. `schema/docs-digger.schema.json` (JSON Schema 2020-12, every property described,
  `additionalProperties: false`), `schema_version: 1`; `tools/check_schema.py` checks the golden files
  (CTest `schema.<name>`) and the CI output.
- [ ] Windows CI job (`windows-latest`) that configures overlunky, builds and runs the digger and
  uploads the JSON as an artifact for the existing Slate job. Decide where the job lives
  (overlunky's `build-docs.yml` or this repository). Written (`.github/workflows/dig.yml`, this
  repository; see section 2) and its build with the package's clang-cl verified locally; open until
  the first run on GitHub passes.
- [x] `README.md` complete for a new contributor: setup, run, tests, quality gate, schema.

Definition of done:

- [x] Every function gets the same group as in `generate.py`, or the difference is listed with a
  reason.
- [x] The parity report covers P1–P15 and has no unexplained gaps. It prints the items it covers
  (P16 is the output stage's).
- [ ] The CI job passes on a real overlunky checkout and its artifact can be downloaded.
- [x] The schema document is committed and matches the output.

### DIG-6. Lua names and LuaLS definition files (P16)

Replaces `generate_emmylua.py` (`docs/game_data/spel2.lua`). That file is 574 KB: classes and
functions take about 445 KB, enums about 130 KB (ENT_TYPE alone 32 KB). It is over the 500 KB default
of LuaLS `Lua.workspace.preloadFileSize`, so the VS Code Lua extension skips it unless the user raises
the setting.

- [ ] Lua naming (P16) as a step after all TUs: port the `replace_table` rules to
  `config/lua_types.json` and add a `lua` spelling to every TypeInfo in the JSON, so DIG-7 uses the same
  names. Built from the type facts, not by string replacement: integers to `integer`, floats to
  `number`, nullable to `T?`, containers to `T[]` / `table<K, V>`, tuples to several returns,
  `std::function` and CallbackSig to `fun(a: A, ...): R`, `sol::object` to `any`, variadic to `...`;
  hidden params are dropped. Pure mapping logic gets GoogleTest unit tests.
- [ ] `src/emit_lua.cpp` (`--lua-out <dir>`) writes `---@meta` files with LuaLS annotations:
  - globals and global variables as `---@type`;
  - functions with `---@param name type? doc`, `---@return type doc` (nil meaning in the text),
    `---@overload fun(...)` for further overloads, `---@deprecated`, doc text, a callback param typed
    as `fun(...)`;
  - usertypes as `---@class Name : Base` with `---@field`; methods as `function Name:m()` when `self`
    is implicit or the first param; constructors as `Name.new`; vtable hooks `set_pre_*` /
    `set_post_*` with their typed callbacks;
  - enums as `---@enum NAME` tables with their values; aliases as `---@alias`; libraries;
  - `/// NoDoc` items left out.
- [ ] Event callbacks (`set_callback(cb, ON.X)`, `CustomTheme:override`): decide in the step how to
  type them so LuaLS shows the per-event signature. Try `---@overload` per event with a literal value
  type in VS Code; if LuaLS cannot tell enum values apart, keep the generic signature and list the
  per-event signatures in the doc text.
- [ ] Split: whole declarations go into files by kind (`spel2.lua` for globals, functions, aliases and
  libraries; `spel2_types.lua`; `spel2_enums.lua`). A file that would go over the limit
  (`--lua-max-file-size`, default 480,000 bytes, a margin under 500 KB) continues in `_2`, `_3`, cut at
  group (types: name) boundaries in a stable order. Each file stands on its own (`---@meta`, no
  `require`). Expected: 3 to 4 files.
- [ ] Tests: a golden `.lua` output for the existing fixtures plus one that crosses a small size
  limit; a CTest that runs `luacheck` (config `tests/.luacheckrc` for definition files) over the golden
  outputs, skipped with a message when luacheck is not found. If `lua-language-server` is installed,
  `--check` on the real output in `dig.bat` reports annotation errors (optional, not a CTest).
- [ ] `tools/parity.py`: every class, field, function and enum value declared in the old `spel2.lua`
  is in the new files, or listed as an accepted difference.

Definition of done:

- [ ] Every output file is under the limit, and luacheck reports nothing on the real overlunky output.
- [ ] In VS Code with the Lua extension at default settings, all files load; hover shows docs and the
  params of a callback (`set_callback`, `set_pre_damage`) are typed.
- [ ] The parity report for the Lua files has no unexplained gaps.

### DIG-7. HTML reference with search

Replaces `generate.py` + Slate. Today Slate (Ruby) builds one 1.7 MB page from generated Markdown;
search is the browser's in-page find, and examples come from `docs/examples/<name>.md`.

Generators checked (2026-11):

- **Astro Starlight (chosen).** Full-text search with Pagefind built in: a static, chunked index that
  loads only the parts a query needs, so it works on large sites with no server. Also has light/dark
  themes, sidebar navigation and code highlighting (Expressive Code). Pages can be generated straight
  from the digger JSON (Astro content collections with the `file()` loader, rendered by components),
  with no Markdown in between. Costs a Node toolchain (pinned with a lockfile).
- **Zensical** (successor of Material for MkDocs, same team; Python, reads `mkdocs.yml`): the fallback
  if Node is unwanted. It is young and lacks full feature parity. Material for MkDocs itself is in
  maintenance mode since November 2025: not for new work.
- **VitePress**: built-in local search (MiniSearch) loads the whole index in the browser; fewer
  reference-doc features than Starlight.
- **Docusaurus**: heavier; search needs Algolia DocSearch (hosted) or a community plugin.
- **Own templates + standalone Pagefind**: the fewest dependencies, but we would write and maintain the
  theme and navigation ourselves.
- **LDoc / LuaLS doc export** from the DIG-6 files: generic output that loses the callback, event and
  group structure.

Steps:

- [ ] Confirm the choice with the overlunky maintainers, since the site replaces the published docs.
- [ ] Isolation: the whole Starlight project (`package.json`, lockfile, config, components, styles,
  the redirect script) lives in `src/html/starlight/`, so the generator can later be swapped or made
  switchable (`src/html/<name>/`). Its only inputs are the digger JSON and `docs/examples/`; nothing
  outside the directory includes or depends on it, and the C++ build doesn't touch it. Anything a
  second generator would need too (page grouping, the old-anchor map, the sample symbol list for
  the checks) is written by the digger or kept as plain data outside the directory, not in Astro code.
- [ ] `src/html/starlight/` with pinned versions and a lockfile; `site.bat [generator] <json>` (default
  `starlight`) runs that generator's build (`npm ci` and `astro build`), output under
  `build/site/<generator>`. Node is a host requirement in `README.md`. Downloads stay under
  `thirdparty/` as for every other tool: `site.bat` installs the packages into
  `thirdparty/node-<generator>/` and links `node_modules` there (directory junction, git-ignored). The clang-format, Lizard and
  clang-tidy gate covers C++ only; `quality.bat` must skip this directory.
- [ ] Pages: home (ported text of `_home.md`), global variables, one page per function group, one per
  usertype (grouped in the sidebar the way `generate.py` groups types), enums, events (`ON`,
  `THEME_OVERRIDE`, each with its callback signature), casting, aliases. A function shows its Lua
  signature (DIG-6 names), params with type, optional and callback signature, returns with the nil
  meaning, deprecated and doc-only marks, a link to the source line on GitHub, and its example from
  `docs/examples/`.
- [ ] Search: symbol names weigh more than prose (`data-pagefind-weight` / meta), so typing a function,
  type or enum value name finds it first; enum values (thousands in ENT_TYPE) are searchable.
- [ ] Links: every type name in a signature links to its page; usertypes show base classes and derived
  types.
- [ ] Old links: the old single-page anchors (`index.html#spawn_entity`) still work through a small
  redirect script that maps anchor ids to the new pages.
- [ ] CI: a site job takes the JSON artifact of the DIG-5 Windows job, builds the site and deploys it
  (GitHub Pages). Replacing the Slate job is decided together with the maintainers.
- [ ] `tools/parity.py`: every anchor of the old `index.html` maps to a page of the new site, or is
  listed as an accepted difference.

Definition of done:

- [ ] The site builds without warnings, and a link check finds no broken internal links.
- [ ] For a sample list of symbols (functions, members, enum values, events), search shows the symbol
  as the first result.
- [ ] Old anchor links from the sample list land on the right item.
- [ ] Building twice from the same JSON gives the same pages.



## 7. Risks and open questions

- **LLVM distribution on Windows.** Confirm the release archive contains the Clang CMake package. If
  it doesn't, fall back to vcpkg `llvm[clang]` (long build, cacheable in CI).
- **Windows-only parsing.** Needs MSVC headers and the Windows SDK, so the docs pipeline gains a
  Windows step (DIG-5: `.github/workflows/dig.yml`). The alternative of cross-parsing on Linux with `clang-cl` + `xwin`-fetched SDK
  headers is more moving parts. Not recommended unless the Windows runner is unacceptable.
- **Parse cost.** sol2 is template-heavy. Measured in DIG-0 (Release, sequential, one TU at a time):
  50 script TUs in 154 s; median 2.2 s, min 1.4 s, max 10.3 s (`entities_monsters_lua.cpp`; then
  `entities_items_lua.cpp` 8.7 s, `vtables_lua.cpp` 8.5 s). Decision: stay sequential. Day-to-day
  work runs on fixtures (under 1 s each), and 2.5 minutes is acceptable for parity runs and CI.
  Revisit with the all-TUs executor if parity runs become the bottleneck; measure memory per TU
  first, since 32 parallel sol2 TUs may not fit. Caching on file hashes, as `parse_cache.py` does
  today, stays an option. DIG-4 widened the selection to all 101 `src/game_api` TUs (events are triggered from
  `state.cpp`, `level_api.cpp`, `render_api.cpp`; all parse with 0 errors): about 7 minutes sequential, the 51
  non-script TUs adding about 140 s. The digger prints a line per TU and a heartbeat every 10 s (single TUs take
  up to 22 s). DIG-4b: with 32 threads a cold run takes about 30 s (peak about 2 GB per sol2 TU, measured on
  `entities_monsters_lua.cpp`); a run without source changes takes 3 s from the cache (20 MB); changing one
  header re-parses only the TUs that read it.
- **Callback dataflow heuristics (C3).** Unusual storage patterns will be missed. C7 fallback and
  the DIG-4 report make misses visible. Don't build general dataflow; add patterns only as the
  report demands.
- **Pinned Clang API.** Bumping the major version may need small fixes. Keep matcher code in two
  files to contain this.
- **Uncommitted local change** in `src/game_api/script/safe_cb.inl` (present before this plan). It's
  unrelated, but it affects what C2/C6 read during development.
