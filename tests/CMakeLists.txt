# Golden tests: docs-digger runs on each fixture and its outputs must equal tests/expected/<name>.json and the Lua
# files in tests/expected/lua/<name>/.
# Fixtures use overlunky's real sol2 headers (OVERLUNKY_ROOT) and the pinned Lua headers from thirdparty/.
dd_fetch_lua("${THIRDPARTY_DIR}")
set(SOL2_INCLUDE_DIR "${OVERLUNKY_ROOT}/src/sol2/include")
if(NOT EXISTS "${SOL2_INCLUDE_DIR}/sol/sol.hpp")
  message(FATAL_ERROR "sol2 not found in ${SOL2_INCLUDE_DIR}; set OVERLUNKY_ROOT to an overlunky checkout "
    "with submodules.")
endif()

# Unit tests of pure helper logic (GoogleTest), linked like the digger itself.
include("${PROJECT_SOURCE_DIR}/cmake/GoogleTest.cmake")
add_executable(docs-digger-unit unit/lua_names_test.cpp)
target_link_libraries(docs-digger-unit PRIVATE docs-digger-lib dd_gtest)
dd_llvm_target_settings(docs-digger-unit)
add_test(NAME unit COMMAND docs-digger-unit)

# The schema tests check the golden files against schema/docs-digger.schema.json with tools/check_schema.py.
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Registers the golden test golden.<name> for tests/fixtures/<fixture>.cpp (FIXTURE, default <name>) with extra digger
# options (ARGS): the JSON is compared with tests/expected/<fixture>.json, the Lua definition files with
# tests/expected/lua/<name>/. A test of its own fixture also gets schema.<name>, the schema check of its golden JSON.
function(dd_add_golden_test name)
  cmake_parse_arguments(PARSE_ARGV 1 golden "" "FIXTURE" "ARGS")
  set(fixture "${name}")
  if(golden_FIXTURE)
    set(fixture "${golden_FIXTURE}")
  else()
    add_test(NAME schema.${name}
      COMMAND Python3::Interpreter "${PROJECT_SOURCE_DIR}/tools/check_schema.py"
        "${PROJECT_SOURCE_DIR}/schema/docs-digger.schema.json" "${CMAKE_CURRENT_SOURCE_DIR}/expected/${name}.json")
  endif()
  string(REPLACE ";" "|" extra_args "${golden_ARGS}")
  add_test(NAME golden.${name}
    COMMAND "${CMAKE_COMMAND}"
      "-DDIGGER=$<TARGET_FILE:docs-digger>"
      "-DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
      "-DFIXTURE=${fixture}"
      "-DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/${fixture}.json"
      "-DACTUAL=${CMAKE_CURRENT_BINARY_DIR}/actual/${name}.json"
      "-DLUA_EXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/lua/${name}"
      "-DLUA_ACTUAL=${CMAKE_CURRENT_BINARY_DIR}/actual/lua/${name}"
      "-DEXTRA_ARGS=${extra_args}"
      "-DSOL2_INCLUDE_DIR=${SOL2_INCLUDE_DIR}"
      "-DLUA_INCLUDE_DIR=${LUA_LOCAL_DIR}/src"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/RunGolden.cmake")
endfunction()

dd_add_golden_test(key_paths)
dd_add_golden_test(functions)
dd_add_golden_test(docs)
dd_add_golden_test(usertypes)
dd_add_golden_test(enums)
dd_add_golden_test(callbacks)
dd_add_golden_test(vtables)
dd_add_golden_test(groups)
# The Lua files cut into several under a small size limit, and in the classic EmmyLua dialect.
dd_add_golden_test(usertypes_split FIXTURE usertypes ARGS --lua-max-file-size=800)
dd_add_golden_test(callbacks_emmylua FIXTURE callbacks ARGS --lua-style=emmylua)

# luacheck over the golden Lua files (tests/.luacheckrc: settings for definition files). luacheck is a LuaRocks
# script: run with lua and the LuaRocks tree's module path, or a luacheck executable. Skipped when neither is found.
find_program(DD_LUA lua)
find_program(DD_LUACHECK_EXE NAMES luacheck.exe luacheck.bat)
find_file(DD_LUACHECK_SCRIPT luacheck PATHS "$ENV{APPDATA}/luarocks/bin" "$ENV{HOME}/.luarocks/bin" NO_DEFAULT_PATH)
add_test(NAME luacheck
  COMMAND "${CMAKE_COMMAND}"
    "-DLUA=${DD_LUA}" "-DLUACHECK_EXE=${DD_LUACHECK_EXE}" "-DLUACHECK_SCRIPT=${DD_LUACHECK_SCRIPT}"
    "-DCONFIG=${CMAKE_CURRENT_SOURCE_DIR}/.luacheckrc" "-DFILES=${CMAKE_CURRENT_SOURCE_DIR}/expected/lua"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/RunLuacheck.cmake")
set_tests_properties(luacheck PROPERTIES SKIP_REGULAR_EXPRESSION "luacheck not found")
