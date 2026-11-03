# Runs docs-digger on one fixture and compares the output with the golden file (cmake -P, see CMakeLists.txt).
# With the environment variable DD_UPDATE_GOLDEN=1 the output replaces the golden file instead: review the diff.
get_filename_component(actual_dir "${ACTUAL}" DIRECTORY)
file(MAKE_DIRECTORY "${actual_dir}")

# A fixture may come with a runtime enum dump, <name>.lua_enums.txt, like overlunky's docs/game_data/lua_enums.txt.
set(runtime_enums "")
if(EXISTS "${FIXTURE_DIR}/${NAME}.lua_enums.txt")
  set(runtime_enums "--runtime-enums=${FIXTURE_DIR}/${NAME}.lua_enums.txt")
endif()

# Same language mode and sol2 settings as overlunky's game_api target.
execute_process(
  COMMAND "${DIGGER}" "--root=${FIXTURE_DIR}" "-o" "${ACTUAL}" ${runtime_enums} "${FIXTURE_DIR}/${NAME}.cpp" --
    -std=c++20 "-I${SOL2_INCLUDE_DIR}" "-I${LUA_INCLUDE_DIR}" -DSOL_ALL_SAFETIES_ON=1 -DSOL_PRINT_ERRORS=0
  RESULT_VARIABLE digger_result)
if(NOT digger_result STREQUAL "0")
  message(FATAL_ERROR "docs-digger failed on ${NAME}.cpp (exit ${digger_result})")
endif()

if("$ENV{DD_UPDATE_GOLDEN}" STREQUAL "1")
  get_filename_component(expected_dir "${EXPECTED}" DIRECTORY)
  file(MAKE_DIRECTORY "${expected_dir}")
  file(COPY_FILE "${ACTUAL}" "${EXPECTED}")
  message(STATUS "Updated ${EXPECTED}")
  return()
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files --ignore-eol "${EXPECTED}" "${ACTUAL}"
  RESULT_VARIABLE compare_result)
if(NOT compare_result STREQUAL "0")
  message(FATAL_ERROR "Output differs from the golden file.\n  expected: ${EXPECTED}\n  actual:   ${ACTUAL}\n"
    "If the change is intended, rerun with DD_UPDATE_GOLDEN=1 and review the diff.")
endif()
