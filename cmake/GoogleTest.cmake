# GoogleTest for the unit tests of pure helper logic (Lua type names). The release is pinned in thirdparty/ like the
# other sources (not FetchContent: its download stamp lives in the build tree, so a deleted build/ would download
# again), and gtest is built here from its single-file source with the settings of the LLVM-linked targets.
include("${CMAKE_CURRENT_LIST_DIR}/Download.cmake")

set(DD_GTEST_DIR "${THIRDPARTY_DIR}/googletest-1.18.0/googletest")
dd_fetch_archive("https://github.com/google/googletest/releases/download/v1.18.0/googletest-1.18.0.tar.gz"
  "6e3191c1455468b3fc35a417fb565c1c5071aee1b7e7f85e30cf48a98d37d8b5" "${THIRDPARTY_DIR}"
  "googletest-1.18.0/googletest/src/gtest-all.cc")

add_library(dd_gtest STATIC "${DD_GTEST_DIR}/src/gtest-all.cc" "${DD_GTEST_DIR}/src/gtest_main.cc")
target_include_directories(dd_gtest SYSTEM PUBLIC "${DD_GTEST_DIR}/include")
target_include_directories(dd_gtest PRIVATE "${DD_GTEST_DIR}")
# Same runtime and no RTTI, as the unit tests link the LLVM libraries.
set_target_properties(dd_gtest PROPERTIES MSVC_RUNTIME_LIBRARY MultiThreaded)
if(MSVC)
  target_compile_options(dd_gtest PRIVATE /GR- /w)
else()
  target_compile_options(dd_gtest PRIVATE -fno-rtti -w)
endif()
