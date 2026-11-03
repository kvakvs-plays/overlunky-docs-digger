# The pinned Node.js for the HTML generators in src/html/ (site.bat): a fixed version, so local builds and CI use the
# same one and the host's Node (if any) does not matter.
# `cmake -DTHIRDPARTY_DIR=<dir> -P cmake/NodePackage.cmake` downloads it once (SHA256 from nodejs.org SHASUMS256.txt).
include("${CMAKE_CURRENT_LIST_DIR}/Download.cmake")

set(DOCS_DIGGER_NODE_VERSION "24.22.2")
set(DOCS_DIGGER_NODE_PACKAGE "node-v${DOCS_DIGGER_NODE_VERSION}-win-x64")

# Downloads the Windows zip and unpacks it into thirdparty_dir/DOCS_DIGGER_NODE_PACKAGE once.
function(dd_fetch_node thirdparty_dir)
    dd_fetch_archive(
        "https://nodejs.org/dist/v${DOCS_DIGGER_NODE_VERSION}/${DOCS_DIGGER_NODE_PACKAGE}.zip"
        "ca1bff5d8ead13c9f2ea404a176db46bd430704d5b5113734049fa365e7799e2"
        "${thirdparty_dir}" "${DOCS_DIGGER_NODE_PACKAGE}/node.exe")
endfunction()

if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
    if(NOT DEFINED THIRDPARTY_DIR)
        message(FATAL_ERROR "THIRDPARTY_DIR is not set.")
    endif()
    dd_fetch_node("${THIRDPARTY_DIR}")
    # site.bat reads the directory from here, so the version is pinned in this file only.
    file(TO_NATIVE_PATH "${THIRDPARTY_DIR}/${DOCS_DIGGER_NODE_PACKAGE}" node_dir)
    file(WRITE "${THIRDPARTY_DIR}/node-path.txt" "${node_dir}")
endif()
