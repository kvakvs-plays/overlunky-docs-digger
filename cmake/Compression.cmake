# zlib and zstd for the Windows LLVM release package: its LLVMSupport links them (CRC and compression) but the
# package ships neither. Sources are pinned in thirdparty/; the libraries are built here with the static runtime
# the LLVM libraries use, under the target names LLVM's exports expect.
include("${CMAKE_CURRENT_LIST_DIR}/Download.cmake")

set(DD_ZLIB_DIR "${THIRDPARTY_DIR}/zlib-1.3.1")
dd_fetch_archive("https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz"
  "9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23" "${THIRDPARTY_DIR}" "zlib-1.3.1/zlib.h")
set(DD_ZSTD_DIR "${THIRDPARTY_DIR}/zstd-1.5.7/lib")
dd_fetch_archive("https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz"
  "eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3" "${THIRDPARTY_DIR}" "zstd-1.5.7/lib/zstd.h")

# Plain source lists instead of the projects' own CMake files: zlib's renames zconf.h inside the source tree.
add_library(dd_zlib STATIC)
foreach(source IN ITEMS adler32 compress crc32 deflate gzclose gzlib gzread gzwrite infback inffast inflate
                        inftrees trees uncompr zutil)
  target_sources(dd_zlib PRIVATE "${DD_ZLIB_DIR}/${source}.c")
endforeach()
target_include_directories(dd_zlib SYSTEM INTERFACE "${DD_ZLIB_DIR}")

file(GLOB DD_ZSTD_SOURCES CONFIGURE_DEPENDS
  "${DD_ZSTD_DIR}/common/*.c" "${DD_ZSTD_DIR}/compress/*.c" "${DD_ZSTD_DIR}/decompress/*.c")
add_library(dd_zstd STATIC ${DD_ZSTD_SOURCES})
# The x86-64 assembly decoder needs a GNU assembler; the C fallback is enough for the few calls Clang makes.
target_compile_definitions(dd_zstd PRIVATE ZSTD_DISABLE_ASM)
target_include_directories(dd_zstd SYSTEM INTERFACE "${DD_ZSTD_DIR}")

set_target_properties(dd_zlib dd_zstd PROPERTIES MSVC_RUNTIME_LIBRARY MultiThreaded)
if(MSVC)
  target_compile_options(dd_zlib PRIVATE /w)
  target_compile_options(dd_zstd PRIVATE /w)
endif()
add_library(ZLIB::ZLIB ALIAS dd_zlib)
add_library(zstd::libzstd_static ALIAS dd_zstd)
