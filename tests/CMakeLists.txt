# Golden tests: docs-digger runs on each fixture and its output must equal tests/expected/<name>.json.
# Fixtures use overlunky's real sol2 headers (OVERLUNKY_ROOT) and the pinned Lua headers from thirdparty/.
dd_fetch_lua("${THIRDPARTY_DIR}")
set(SOL2_INCLUDE_DIR "${OVERLUNKY_ROOT}/src/sol2/include")
if(NOT EXISTS "${SOL2_INCLUDE_DIR}/sol/sol.hpp")
  message(FATAL_ERROR "sol2 not found in ${SOL2_INCLUDE_DIR}; set OVERLUNKY_ROOT to an overlunky checkout "
    "with submodules.")
endif()

# Registers the golden test for tests/fixtures/<name>.cpp.
function(dd_add_golden_test name)
  add_test(NAME golden.${name}
    COMMAND "${CMAKE_COMMAND}"
      "-DDIGGER=$<TARGET_FILE:docs-digger>"
      "-DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
      "-DNAME=${name}"
      "-DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/${name}.json"
      "-DACTUAL=${CMAKE_CURRENT_BINARY_DIR}/actual/${name}.json"
      "-DSOL2_INCLUDE_DIR=${SOL2_INCLUDE_DIR}"
      "-DLUA_INCLUDE_DIR=${LUA_LOCAL_DIR}/src"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/RunGolden.cmake")
endfunction()

dd_add_golden_test(key_paths)
dd_add_golden_test(functions)
dd_add_golden_test(docs)
dd_add_golden_test(usertypes)
