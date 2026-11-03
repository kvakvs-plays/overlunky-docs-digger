# Runs docs-digger on one fixture and compares the output with the golden file (cmake -P, see CMakeLists.txt).
# With the environment variable DD_UPDATE_GOLDEN=1 the output replaces the golden file instead: review the diff.
# The fixture then goes through the result cache twice: the first run fills it, the second must take the result from
# it and give the same output (a model field the cache codec misses shows up here).
get_filename_component(actual_dir "${ACTUAL}" DIRECTORY)
file(MAKE_DIRECTORY "${actual_dir}")

# A fixture may come with a runtime enum dump, <name>.lua_enums.txt, like overlunky's docs/game_data/lua_enums.txt.
set(fixture_options "")
if(EXISTS "${FIXTURE_DIR}/${NAME}.lua_enums.txt")
  set(fixture_options "--runtime-enums=${FIXTURE_DIR}/${NAME}.lua_enums.txt")
endif()
# ... and with function group rules, <name>.groups.json, like config/groups.json.
if(EXISTS "${FIXTURE_DIR}/${NAME}.groups.json")
  list(APPEND fixture_options "--groups=${FIXTURE_DIR}/${NAME}.groups.json")
endif()

# Runs the digger with extra options, writing output; its progress lines go to the variable named by log_var.
function(run_digger output log_var)
  # Same language mode and sol2 settings as overlunky's game_api target.
  execute_process(
    COMMAND "${DIGGER}" "--root=${FIXTURE_DIR}" "-o" "${output}" ${fixture_options} ${ARGN}
      "${FIXTURE_DIR}/${NAME}.cpp" --
      -std=c++20 "-I${SOL2_INCLUDE_DIR}" "-I${LUA_INCLUDE_DIR}" -DSOL_ALL_SAFETIES_ON=1 -DSOL_PRINT_ERRORS=0
    RESULT_VARIABLE digger_result
    ERROR_VARIABLE digger_log)
  if(NOT digger_result STREQUAL "0")
    message(FATAL_ERROR "docs-digger failed on ${NAME}.cpp (exit ${digger_result}):\n${digger_log}")
  endif()
  set(${log_var} "${digger_log}" PARENT_SCOPE)
endfunction()

# Fails unless output equals the golden file.
function(compare_with_golden output what)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files --ignore-eol "${EXPECTED}" "${output}"
    RESULT_VARIABLE compare_result)
  if(NOT compare_result STREQUAL "0")
    message(FATAL_ERROR "Output ${what} differs from the golden file.\n  expected: ${EXPECTED}\n  actual:   ${output}\n"
      "If the change is intended, rerun with DD_UPDATE_GOLDEN=1 and review the diff.")
  endif()
endfunction()

run_digger("${ACTUAL}" log)
if("$ENV{DD_UPDATE_GOLDEN}" STREQUAL "1")
  get_filename_component(expected_dir "${EXPECTED}" DIRECTORY)
  file(MAKE_DIRECTORY "${expected_dir}")
  file(COPY_FILE "${ACTUAL}" "${EXPECTED}")
  message(STATUS "Updated ${EXPECTED}")
  return()
endif()
compare_with_golden("${ACTUAL}" "")

set(cache "${ACTUAL}.sqlite")
file(REMOVE "${cache}" "${cache}-wal" "${cache}-shm")
run_digger("${ACTUAL}.filled" log "--cache=${cache}")
compare_with_golden("${ACTUAL}.filled" "while filling the cache")
run_digger("${ACTUAL}.cached" log "--cache=${cache}")
if(NOT log MATCHES "cached")
  message(FATAL_ERROR "The second run did not take ${NAME}.cpp from the cache:\n${log}")
endif()
compare_with_golden("${ACTUAL}.cached" "from the cache")
