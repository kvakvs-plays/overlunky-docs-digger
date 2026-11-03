# Runs docs-digger on one fixture and compares the output with the golden files (cmake -P, see CMakeLists.txt): the
# JSON with EXPECTED, the Lua definition files with the ones in LUA_EXPECTED.
# With the environment variable DD_UPDATE_GOLDEN=1 the output replaces the golden files instead: review the diff.
# The fixture then goes through the result cache twice: the first run fills it, the second must take the result from
# it and give the same output (a model field the cache codec misses shows up here).
# Inputs: DIGGER, FIXTURE_DIR, FIXTURE (fixture name), EXPECTED, ACTUAL, LUA_EXPECTED, LUA_ACTUAL, SOL2_INCLUDE_DIR,
# LUA_INCLUDE_DIR, and EXTRA_ARGS (digger options separated by `|`).
get_filename_component(actual_dir "${ACTUAL}" DIRECTORY)
file(MAKE_DIRECTORY "${actual_dir}")

# A fixture may come with a runtime enum dump, <fixture>.lua_enums.txt, like overlunky's docs/game_data/lua_enums.txt.
set(fixture_options "")
if(EXISTS "${FIXTURE_DIR}/${FIXTURE}.lua_enums.txt")
  set(fixture_options "--runtime-enums=${FIXTURE_DIR}/${FIXTURE}.lua_enums.txt")
endif()
# ... and with function group rules, <fixture>.groups.json, like config/groups.json.
if(EXISTS "${FIXTURE_DIR}/${FIXTURE}.groups.json")
  list(APPEND fixture_options "--groups=${FIXTURE_DIR}/${FIXTURE}.groups.json")
endif()
string(REPLACE "|" ";" extra_args "${EXTRA_ARGS}")
list(APPEND fixture_options ${extra_args})

# Runs the digger with extra options, writing output; its progress lines go to the variable named by log_var.
function(run_digger output log_var)
  # Same language mode and sol2 settings as overlunky's game_api target.
  execute_process(
    COMMAND "${DIGGER}" "--root=${FIXTURE_DIR}" "-o" "${output}" ${fixture_options} ${ARGN}
      "${FIXTURE_DIR}/${FIXTURE}.cpp" --
      -std=c++20 "-I${SOL2_INCLUDE_DIR}" "-I${LUA_INCLUDE_DIR}" -DSOL_ALL_SAFETIES_ON=1 -DSOL_PRINT_ERRORS=0
    RESULT_VARIABLE digger_result
    ERROR_VARIABLE digger_log)
  if(NOT digger_result STREQUAL "0")
    message(FATAL_ERROR "docs-digger failed on ${FIXTURE}.cpp (exit ${digger_result}):\n${digger_log}")
  endif()
  set(${log_var} "${digger_log}" PARENT_SCOPE)
endfunction()

# Fails unless the file actual equals the golden file expected.
function(compare_file expected actual what)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files --ignore-eol "${expected}" "${actual}"
    RESULT_VARIABLE compare_result)
  if(NOT compare_result STREQUAL "0")
    message(FATAL_ERROR "Output ${what} differs from the golden file.\n  expected: ${expected}\n  actual:   ${actual}\n"
      "If the change is intended, rerun with DD_UPDATE_GOLDEN=1 and review the diff.")
  endif()
endfunction()

# Fails unless the Lua files written equal the golden ones, file by file, with the same file names.
function(compare_lua_files)
  file(GLOB expected_files RELATIVE "${LUA_EXPECTED}" "${LUA_EXPECTED}/*.lua")
  file(GLOB actual_files RELATIVE "${LUA_ACTUAL}" "${LUA_ACTUAL}/*.lua")
  if(NOT expected_files STREQUAL actual_files)
    message(FATAL_ERROR "Lua files differ from the golden ones.\n  expected: ${expected_files}\n  actual:   "
      "${actual_files}\nIf the change is intended, rerun with DD_UPDATE_GOLDEN=1 and review the diff.")
  endif()
  foreach(name IN LISTS expected_files)
    compare_file("${LUA_EXPECTED}/${name}" "${LUA_ACTUAL}/${name}" "${name}")
  endforeach()
endfunction()

file(REMOVE_RECURSE "${LUA_ACTUAL}")
run_digger("${ACTUAL}" log "--lua-out=${LUA_ACTUAL}")
if("$ENV{DD_UPDATE_GOLDEN}" STREQUAL "1")
  get_filename_component(expected_dir "${EXPECTED}" DIRECTORY)
  file(MAKE_DIRECTORY "${expected_dir}")
  file(COPY_FILE "${ACTUAL}" "${EXPECTED}")
  file(REMOVE_RECURSE "${LUA_EXPECTED}")
  file(COPY "${LUA_ACTUAL}/" DESTINATION "${LUA_EXPECTED}")
  message(STATUS "Updated ${EXPECTED} and ${LUA_EXPECTED}")
  return()
endif()
compare_file("${EXPECTED}" "${ACTUAL}" "")
compare_lua_files()

set(cache "${ACTUAL}.sqlite")
file(REMOVE "${cache}" "${cache}-wal" "${cache}-shm")
run_digger("${ACTUAL}.filled" log "--cache=${cache}")
compare_file("${EXPECTED}" "${ACTUAL}.filled" "while filling the cache")
run_digger("${ACTUAL}.cached" log "--cache=${cache}")
if(NOT log MATCHES "cached")
  message(FATAL_ERROR "The second run did not take ${FIXTURE}.cpp from the cache:\n${log}")
endif()
compare_file("${EXPECTED}" "${ACTUAL}.cached" "from the cache")
