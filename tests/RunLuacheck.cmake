# Runs luacheck over the golden Lua files (cmake -P, see tests/CMakeLists.txt). Prints "luacheck not found" (CTest then
# reports the test as skipped) when neither a luacheck executable nor lua with the LuaRocks luacheck script exists.
# Inputs: LUA, LUACHECK_EXE, LUACHECK_SCRIPT (any may be empty or *-NOTFOUND), CONFIG (.luacheckrc), FILES (directory).
if(LUACHECK_EXE)
  set(command "${LUACHECK_EXE}")
elseif(LUA AND LUACHECK_SCRIPT)
  # The script loads its modules from its LuaRocks tree: <tree>/bin/luacheck reads <tree>/share/lua/5.4.
  get_filename_component(tree "${LUACHECK_SCRIPT}" DIRECTORY)
  get_filename_component(tree "${tree}" DIRECTORY)
  set(ENV{LUA_PATH} "${tree}/share/lua/5.4/?.lua;${tree}/share/lua/5.4/?/init.lua;;")
  set(ENV{LUA_CPATH} "${tree}/lib/lua/5.4/?.dll;${tree}/lib/lua/5.4/?.so;;")
  set(command "${LUA}" "${LUACHECK_SCRIPT}")
else()
  message(STATUS "luacheck not found: install it with LuaRocks (luarocks install luacheck)")
  return()
endif()
# Relative paths from the config's directory: luacheck does not find a config given as an absolute Windows path.
get_filename_component(config_dir "${CONFIG}" DIRECTORY)
get_filename_component(config_name "${CONFIG}" NAME)
file(RELATIVE_PATH files "${config_dir}" "${FILES}")
if(files STREQUAL "")
  set(files .) # FILES is the config's own directory (examples/)
endif()
execute_process(
  COMMAND ${command} --config "${config_name}" --no-color "${files}"
  WORKING_DIRECTORY "${config_dir}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE output)
if(NOT result STREQUAL "0")
  message(FATAL_ERROR "luacheck found problems:\n${output}")
endif()
