# docs-digger: work plan

Build a new standalone CMake C++ tool, `docs-digger`. It lives outside the overlunky and Playlunky trees,
at `F:\Projects\Spelunky\docs-digger`, and gets the overlunky source root (and its
`compile_commands.json`) as an input path. It parses the overlunky sources with Clang and
extracts the Lua API into a structured data file. It replaces the regex-based extraction in
`docs/parse_source.py`: it must find at least everything that file finds, and add the details the
regexes can't see, mainly:

- exact parameter and return types, including nullable/optional parameters and `nil` returns;
- signatures of Lua callbacks (handler functions): what arguments they get and what they may return,
  including the common "return nothing (`nil`)" case and what the C++ side does when `nil` comes back.

This plan covers **data extraction only**. The output format and rendering will be decided later. The
extracted data just has to carry every detail the current generator has, plus the new ones.

Guiding rule: **write as little code as possible.** Use Clang LibTooling, AST matchers and LLVM's
support libraries for everything they can do. Write custom code only for the sol2-specific binding
patterns.

---

## 1. What the current generator extracts (parity checklist)

Taken from `docs/parse_source.py` and `docs/generate.py`. Every item must be present in the new data.

| # | Item | How Python finds it today |
|---|------|---------------------------|
| P1 | Global functions `lua["name"] = ...` (lambda, function reference, `sol::overload`) with param names, param types, return type, `///` comment | regex over `api_files` |
| P2 | Namespaced/member functions `lua["Type"]["name"] = ...`, resolved through the `rpc` list from headers | regex plus header-signature lookup |
| P3 | Deprecated functions (`/// Deprecated`, `deprecated_func.cpp`) | comment directive |
| P4 | `/// NoDoc` items are hidden; directives `Returns`, `Default`, `note:`, `Warning:` are kept in the text | substring checks |
| P5 | Usertypes `new_usertype<T>("Name", "key", value, ...)`: fields, methods, `sol::property` getters and setters, readonly, overloads, constructors, `sol::bases<...>`, `MakeParticleMemberAccess` | regex plus bracket counting |
| P6 | C++ class/struct members (fields and methods, including those inside unions and anonymous structs) with their `///` comments; used to document usertype members | line-based class scanner over `header_files` |
| P7 | Vtable hooks: `VTableEntry<"name", idx, MemFun<&T::f> or Sig, BackBinder<...>>` become `set_pre_<name>` / `set_post_<name>` per usertype; `/// NoDoc` per entry | regex in `vtables_lua.cpp`, `theme_vtable_lua.hpp` |
| P8 | Events / doc-only statements `lua["NAME"];` with comments | regex |
| P9 | Enums: `create_named_table("NAME", "K", v, ...)`, `/* ... */` value-list comments, plus runtime-filled enums from `docs/game_data/lua_enums.txt` | regex plus text file |
| P10 | Entity casts `lua["Entity"]["as_*"]` | regex |
| P11 | Lua libraries from `open_libraries(...)` | regex |
| P12 | Aliases `using X = Y;` | regex |
| P13 | Global variables (`players`, `state`, `game_manager`, `online`, `savegame`, `options`, `meta`, `prng`) | hardcoded `not_functions` list |
| P14 | Function groups ("Message functions", "Generic functions", ...) | hardcoded name-substring table in `generate.py` (`func_cats`) |
| P15 | Callback signatures | **only** parsed from comment prose: `The callback signature is <ret> <name>(<params>)` (`reSignature`) |
| P16 | C++ to Lua type renaming (`replace_table`) | string replace |

P16 is a presentation concern. The digger emits C++ types plus structured type facts (see section 4),
and the Lua naming is applied at the output stage, reusing the existing `replace_table` rules.

---

## 2. Technology choice

**Clang LibTooling + ASTMatchers (C++ API), with output written using `llvm::json`.**

- `clang::tooling::CommonOptionsParser` + `ClangTool` read `compile_commands.json` and handle all of
  CLI, include paths and defines. We write no build-system glue.
- `ast_matchers::MatchFinder` lets each extraction pass be a declarative matcher plus a short callback.
  This is far less code than walking cursors with the libclang C API.
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
- Parallelism over translation units, if needed: `clang::tooling::AllTUsToolExecutor`
  (`--executor=all-TUs --execute-concurrency=N`). Start with plain sequential `ClangTool`.
- Output: `llvm::json::OStream` / `llvm::json::Value`. No third-party JSON dependency.
- Tests: golden-file comparison through CTest (`cmake -E compare_files`). No lit/FileCheck setup.

### Toolchain acquisition

- Pin **one** LLVM major version (proposal: 23.x, matching the local install). The Clang C++ API is not
  stable across majors.
- The local `C:\Program Files\LLVM` installer ships no Clang C++ libraries and no `ClangConfig.cmake`,
  so it can't be used. Use the official `clang+llvm-<ver>-x86_64-pc-windows-msvc.tar.xz` release
  archive, which ships the static libraries and `lib/cmake/{llvm,clang}`. **(Verify on M0.)** Point
  CMake at it with `-DClang_DIR=<root>/lib/cmake/clang`.
- `CMakeLists.txt` is roughly 20 lines: `find_package(Clang REQUIRED CONFIG)`, `add_executable`,
  `target_link_libraries(... clangTooling clangASTMatchers clangFrontend clangAST clangBasic)`, and
  `LLVM_ENABLE_RTTI`/`/EHsc` flags copied from `LLVMConfig`.

### Input: compilation database

- Overlunky is Windows-only (Windows SDK, Detours, MSVC STL). The digger must therefore run on
  **Windows**, with clang-cl driver semantics and the MSVC headers available.
- Produce `compile_commands.json` by configuring overlunky with
  `cmake -G Ninja -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`. Only configure;
  no build is needed unless a target turns out to have generated headers. **(Verify on M0.)**
- Process every translation unit under `src/game_api/script/**` (and `src/game_api/*.cpp` where
  needed) instead of the hand-maintained `header_files`/`api_files` lists. Headers are reached through
  includes. Results are filtered to declarations and statements located under `src/game_api/`.
- **CI consequence:** `build-docs.yml` runs on `ubuntu-latest`. Add a `windows-latest` job that
  configures overlunky, runs the digger and uploads the JSON as an artifact for the existing Slate job.
  This is decided at M5 and doesn't block extraction work.

---

## 3. Project layout

```
F:\Projects\Spelunky\docs-digger\   (separate from overlunky; takes the overlunky root as an argument)
  CMakeLists.txt          standalone; downloads pinned Clang/LLVM into thirdparty/ on Windows
  thirdparty/             git-ignored; unpacked clang+llvm release
  README.md               how to get LLVM, generate compile_commands.json, run
  config/groups.json      function-group rules ported from generate.py func_cats (P14)
  src/main.cpp            CommonOptionsParser, ClangTool, register passes, merge, write JSON
  src/model.hpp           plain structs for the data model + toJSON() overloads (llvm::json)
  src/types.cpp           QualType -> TypeInfo (section 4)
  src/comments.cpp        comment lookup for decls/statements, directive parsing
  src/bindings.cpp        passes B1-B4 (lua[...] = ..., usertypes, enums, misc)
  src/callbacks.cpp       passes C1-C6 (callback signatures, nil semantics)
  tests/fixtures/*.cpp    small sources using the real sol2 headers from <overlunky>/src/sol2
  tests/expected/*.json   golden outputs
  tools/parity.py         loads <overlunky>/docs/parse_source.py results, diffs them with digger JSON (reuses the old parser as the oracle)
```

Expected size: about 1,500 to 2,000 lines of C++ in total. Most of it goes to sol2 pattern resolution
and callback discovery.

---

## 4. Data model (content, not the final format)

Every entity carries `source {file, line}` and `doc {lines[], directives{NoDoc, Deprecated, Returns,
Default, Group, ...}}`.

- **TypeInfo**: `spelling` (as written), `canonical`, `kind`
  (`void | bool | int | float | string | enum | record | pointer | optional | container | tuple | function | lua_object | variadic`),
  `inner[]` (element, optional payload, tuple members, function signature), `nullable` (bool), and
  `hidden` (a sol2 injected parameter such as `sol::this_state` or `sol::this_environment`, which is not
  visible from Lua).
- **Param**: `name`, `type: TypeInfo`, `nullable` (std/sol `optional<T>`, raw pointer that sol maps
  `nil` to `nullptr`, `sol::object`), `omittable` (trailing nullable params), `variadic`
  (`sol::variadic_args`), `callback: CallbackSig?`.
- **FunctionSig**: `params[]`, `returns: TypeInfo[]` (a tuple/pair becomes multiple returns; `optional`
  means it may return `nil`; `void` means it returns nothing).
- **Function**: `lua_name`, `owner` (global, namespace table, or usertype), `group`, `overloads[]: FunctionSig`,
  `cpp_target` (qualified name of the bound C++ function or lambda location), `deprecated`, `nodoc`.
- **Usertype**: `lua_name`, `cpp_type`, `bases[]`, `constructors[]: FunctionSig`, `members[]`
  (`kind: field | method | property | readonly | overload_set`, `type`/`sig`, `getter`/`setter`
  presence, doc taken from the C++ member decl when bound through `&T::m`).
- **CallbackSig**: `params[]` (with front/back binder params marked), `returns`, `nil_allowed`,
  `nil_meaning` (for example "treated as false", "original call proceeds"), `origin`
  (`std_function | make_safe_cb | stored_handle_function | event_dispatch | vtable_hook | doc_comment`),
  `conflicts[]` (disagreement with the prose comment).
- **Enum**: `lua_name`, `values[] {name, value?, doc}`, `origin` (`static | runtime_file`).
- **Event**: `ON.<name>`, `callback: CallbackSig`, `doc`.
- **Alias**, **GlobalVariable** (`lua_name`, `type`), **LuaLibrary**.

---

## 5. Extraction passes

### Binding passes

**B1. Lua table assignments (P1, P2, P8, P10, P13).**
Matcher: `cxxOperatorCallExpr(hasOverloadedOperatorName("="))` whose LHS is a chain of `operator[]`
with string-literal keys on `sol::state`, `sol::state_view`, `sol::table` or `sol::usertype`. Collect
the key path, then resolve the RHS through one shared `resolveBound(Expr*)` helper:

- `LambdaExpr` → `getCallOperator()`.
- `DeclRefExpr` / `UnaryOperator(&)` → `FunctionDecl` / `CXXMethodDecl` / `FieldDecl`.
- `static_cast<Sig>(&f)` → the selected overload (Clang has already resolved it).
- `sol::overload(a, b, ...)` → resolve each argument.
- `sol::property(get[, set])`, `sol::readonly(&T::f)`, `sol::var(...)`.
- Anything else is a value, so it's recorded as a global variable or constant with its type. This
  replaces the `not_functions` list.

Expression statements of the form `lua["NAME"];` (P8) are matched separately and only carry docs.

**B2. Usertypes (P5, P6).**
- `new_usertype<T>(name, k, v, k, v, ...)`: walk the argument pairs and feed each value to
  `resolveBound`.
- Special keys: `sol::base_classes` / `sol::bases<...>`, `sol::meta_function::*`, `sol::constructors<...>`,
  `sol::no_constructor`, `sol::call_constructor`.
- Later assignments on the returned usertype object (`ut["k"] = v`, `ut.set(k, v)`) go through B1.
- Docs for `&T::member` come from the `FieldDecl`/`CXXMethodDecl` comment, which replaces the
  line-based class scanner. Unions and anonymous structs need nothing special in Clang.
- `MakeParticleMemberAccess<...>()` is handled as one small special case.

**B3. Enums (P9, P12).**
- `create_named_table(name, k, v, ...)` and `lua["ENUM"]["K"] = v`: values via `EvaluateAsInt`.
- `/* ... */` value-list comments: port the existing logic if any are still needed after the static
  values are in; check at M3.
- Runtime enums (ENT_TYPE and so on) still come from `docs/game_data/lua_enums.txt` and are merged
  with `origin=runtime_file`.
- Aliases: `TypeAliasDecl` in `src/game_api`, keeping only those referenced by some Lua-visible type.

**B4. Libraries (P11).** Arguments of `open_libraries(...)`.

### Callback passes

These are the new part. Each one fills `Param.callback` for any parameter that receives a Lua function.

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
- In the binding lambda, find the record construction (`CXXConstructExpr` or `InitListExpr`) that the
  `sol::function` parameter flows into, and note the record type `R` and field `F`.
- Across all TUs, find `callExpr(callee(functionDecl(hasName("handle_function"))))` whose function
  argument is `memberExpr(member(F))` on an object of type `R`. Take `Ret` from the template argument
  and the argument types from the call.
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
  `operator bool` branches ("nil = no override"), and `handle_function<void>` (return value ignored).
- For `make_safe_cb` with a non-optional `R`, mark the return as "required; nil converts by sol2
  rules". Exact semantics are to be confirmed by reading `safe_cb.inl`.

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

---

## 6. Milestones

Each milestone ends with golden tests for the newly covered patterns and an updated `tools/parity.py`
report.

**M0. Toolchain and skeleton**
- Get the pinned LLVM/Clang development archive and confirm `find_package(Clang)` works.
- Generate overlunky's `compile_commands.json` (clang-cl + Ninja, configure only).
- Skeleton tool: parse all `src/game_api/script/**` TUs **without errors** (diagnostics count = 0;
  add `--extra-arg` flags if needed) and print every `lua[...] = ...` key path with its location.
- Measure parse time per TU, then decide whether parallel execution is needed now.

**M1. Functions (B1)**
- Global and namespaced functions: overloads, params, returns, nullability, hidden params, comments,
  directives, deprecated, `NoDoc`. Write the data model plus JSON writer.
- `tools/parity.py`: function-name sets and param counts against `ps.funcs`, `ps.deprecated_funcs`, `rpc`.

**M2. Usertypes (B2)**
- Usertypes, members, properties, constructors, bases, and member docs taken from declarations.
- Parity against `ps.types` and `ps.classes`.

**M3. Enums, aliases, libraries, global variables, casts, events (B3, B4)**
- Merge the runtime enum file.
- Parity against `ps.enums`, `ps.aliases`, `ps.lualibs`, `ps.known_casts`, `ps.events`.

**M4. Callback signatures (C1 to C7)**
- Callback signatures, `nil` semantics, prose cross-check, per-event signatures, vtable hook signatures.
- Report: number of callback parameters resolved per origin, and the list of conflicts with prose.

**M5. Grouping, finalization, CI**
- Groups config. Freeze the JSON schema for the output discussion.
- Windows CI job that produces the JSON artifact.

**After M5:** discuss the output and rendering (richer docs, Lua type stubs, and so on). This is out of
scope here.

---

## 7. Risks and open questions

- **LLVM distribution on Windows.** Confirm the release archive contains the Clang CMake package. If it
  doesn't, fall back to vcpkg `llvm[clang]` (long build, cacheable in CI).
- **Windows-only parsing.** Needs MSVC headers and the Windows SDK, so the docs pipeline gains a Windows
  step. The alternative of cross-parsing on Linux with `clang-cl` + `xwin`-fetched SDK headers is more
  moving parts. Not recommended unless the Windows runner is unacceptable.
- **Parse cost.** sol2 is template-heavy, and about 50 TUs at -fsyntax-only may take minutes.
  Mitigations: the all-TUs executor; caching on file hashes, as `parse_cache.py` does today.
- **Callback dataflow heuristics (C3).** Unusual storage patterns will be missed. C7 fallback and the
  M4 report make misses visible. Don't build general dataflow; add patterns only as the report demands.
- **Pinned Clang API.** Bumping the major version may need small fixes. Keep matcher code in two files
  to contain this.
- **Uncommitted local change** in `src/game_api/script/safe_cb.inl` (present before this plan). It's
  unrelated, but it affects what C2/C6 read during development.
