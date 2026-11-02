# Pinned downloads into thirdparty/ (usable from CMakeLists.txt and from cmake -P scripts).
# Everything fetched here survives a deleted build/ directory; see AGENTS.md.

# Downloads URL to DESTINATION once and checks its SHA256; a partial download never takes the final name.
function(dd_fetch_file url sha256 destination)
    if(EXISTS "${destination}")
        return()
    endif()
    get_filename_component(name "${destination}" NAME)
    message(STATUS "Downloading ${name}...")
    file(DOWNLOAD "${url}" "${destination}.part" EXPECTED_HASH SHA256=${sha256} SHOW_PROGRESS STATUS status)
    list(GET status 0 code)
    if(NOT code EQUAL 0)
        file(REMOVE "${destination}.part")
        message(FATAL_ERROR "Downloading ${url} failed: ${status}")
    endif()
    file(RENAME "${destination}.part" "${destination}")
endfunction()

# Unpacks the archive at URL into thirdparty_dir unless thirdparty_dir/marker exists; the archive is deleted after.
function(dd_fetch_archive url sha256 thirdparty_dir marker)
    if(EXISTS "${thirdparty_dir}/${marker}")
        return()
    endif()
    get_filename_component(name "${url}" NAME)
    set(archive "${thirdparty_dir}/${name}")
    dd_fetch_file("${url}" "${sha256}" "${archive}")
    message(STATUS "Unpacking ${name}...")
    file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${thirdparty_dir}")
    if(NOT EXISTS "${thirdparty_dir}/${marker}")
        message(FATAL_ERROR "${name} has no ${marker} after unpacking")
    endif()
    file(REMOVE "${archive}")
endfunction()

# Lua sources that sol2 needs for its headers: fetched here because sol2's own ExternalProject downloads into the
# build tree. Sets LUA_LOCAL_DIR (the layout sol2's LUA_LOCAL_DIR option expects) in the caller.
set(DD_LUA_VERSION "5.4.6")
function(dd_fetch_lua thirdparty_dir)
    dd_fetch_archive("https://www.lua.org/ftp/lua-${DD_LUA_VERSION}.tar.gz"
        "7d5ea1b9cb6aa0b59ca3dde1c6adcb57ef83a1ba8e5432c0ecd06bf439b3ad88"
        "${thirdparty_dir}" "lua-${DD_LUA_VERSION}/src/lua.hpp")
    # sol2 copies <dir>/src and <dir>/include; the Lua tarball has no include/.
    file(MAKE_DIRECTORY "${thirdparty_dir}/lua-${DD_LUA_VERSION}/include")
    set(LUA_LOCAL_DIR "${thirdparty_dir}/lua-${DD_LUA_VERSION}" PARENT_SCOPE)
endfunction()
