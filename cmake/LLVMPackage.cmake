# The pinned Clang/LLVM development package for Windows: static libraries and CMake configs to link the digger, plus
# clang-cl and lld-link to compile it with. The Clang C++ API is not stable across majors: bump the pin only on
# purpose, together with the analyzer pins in tools/requirements-quality.txt.
# CMakeLists.txt includes this file. `cmake -DTHIRDPARTY_DIR=<dir> -P cmake/LLVMPackage.cmake` only downloads the
# package: CI does that first and compiles with the package's own clang-cl (tools/dev-env.cmd, DD_LLVM_BIN).
include("${CMAKE_CURRENT_LIST_DIR}/Download.cmake")

set(DOCS_DIGGER_LLVM_VERSION "23.1.2")
set(DOCS_DIGGER_LLVM_PACKAGE "clang+llvm-${DOCS_DIGGER_LLVM_VERSION}-x86_64-pc-windows-msvc")

# Downloads the package (~860 MB, SHA256-checked) and unpacks it into thirdparty_dir/DOCS_DIGGER_LLVM_PACKAGE once.
function(dd_fetch_llvm thirdparty_dir)
    dd_fetch_archive(
        "https://github.com/llvm/llvm-project/releases/download/llvmorg-${DOCS_DIGGER_LLVM_VERSION}/${DOCS_DIGGER_LLVM_PACKAGE}.tar.xz"
        "8fb91cdc44fcbbdcf6b3ffd0a1f9859abd14a3c3aae4423c2b6d4a4f90bf0095"
        "${thirdparty_dir}" "${DOCS_DIGGER_LLVM_PACKAGE}/lib/cmake/clang/ClangConfig.cmake")
endfunction()

if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
    if(NOT DEFINED THIRDPARTY_DIR)
        message(FATAL_ERROR "THIRDPARTY_DIR is not set.")
    endif()
    dd_fetch_llvm("${THIRDPARTY_DIR}")
    message(STATUS "LLVM package: ${THIRDPARTY_DIR}/${DOCS_DIGGER_LLVM_PACKAGE}")
endif()
