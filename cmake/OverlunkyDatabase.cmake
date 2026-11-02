# Configures overlunky (configure only, no build) to get its compile_commands.json (cmake -P).
# Inputs: OVERLUNKY_ROOT, BUILD_DIR (overlunky's build tree), THIRDPARTY_DIR. Needs the vcvars64 environment.
# overlunky's configure would download Lua (at build time) and a formatting script into its build tree; both are
# pinned in thirdparty/ instead, so deleting build/ never forces a download.
include("${CMAKE_CURRENT_LIST_DIR}/Download.cmake")
foreach(input IN ITEMS OVERLUNKY_ROOT BUILD_DIR THIRDPARTY_DIR)
    if(NOT DEFINED ${input})
        message(FATAL_ERROR "${input} is not set.")
    endif()
endforeach()
if(NOT EXISTS "${OVERLUNKY_ROOT}/src/sol2/include/sol/sol.hpp")
    message(FATAL_ERROR "No overlunky checkout with submodules at ${OVERLUNKY_ROOT}.")
endif()

dd_fetch_lua("${THIRDPARTY_DIR}")
# overlunky's cmake/clang-format.cmake downloads this unless it is already in the build tree.
set(format_script "${THIRDPARTY_DIR}/format-cmake-1.7.1/git-clang-format.py")
dd_fetch_file("https://raw.githubusercontent.com/TheLartians/Format.cmake/v1.7.1/git-clang-format.py"
    "2c2340c00fa632ed1531c027e1683266509dbf27bd276f8dcd1c5644bebad0a4" "${format_script}")
file(COPY "${format_script}" DESTINATION "${BUILD_DIR}")

# Configured every time: overlunky globs its sources, so new files only reach the database through a configure.
execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${OVERLUNKY_ROOT}" -B "${BUILD_DIR}" -G Ninja -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
        "-DLUA_LOCAL_DIR=${LUA_LOCAL_DIR}"
    OUTPUT_QUIET
    RESULT_VARIABLE configure_result)
if(NOT configure_result STREQUAL "0")
    message(FATAL_ERROR "Configuring overlunky failed; rerun without OUTPUT_QUIET to see why.")
endif()
message(STATUS "overlunky compile database: ${BUILD_DIR}/compile_commands.json")
