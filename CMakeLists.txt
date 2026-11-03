cmake_minimum_required(VERSION 3.20)
project(docs-digger C CXX)

# C++23: complete enough in MSVC, clang-cl and GCC >= 14 / Clang >= 19 (Debian 13, Ubuntu 24.04 with g++-14,
# Ubuntu 26.04). C++26 is not yet complete in any stable toolchain.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Everything downloaded (LLVM, FetchContent sources, the analyzer venv) lives here, so build/ stays disposable.
set(THIRDPARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty")

include(cmake/Download.cmake)

# Prebuilt Clang/LLVM development package (static libs + CMake configs).
# On Windows it is downloaded once into thirdparty/ unless Clang_DIR is given.
# On Linux, Clang comes from distro packages or the Docker image (Clang_DIR / CMAKE_PREFIX_PATH).
set(DOCS_DIGGER_LLVM_VERSION "23.1.2")

if(WIN32 AND NOT Clang_DIR)
  set(LLVM_PACKAGE "clang+llvm-${DOCS_DIGGER_LLVM_VERSION}-x86_64-pc-windows-msvc")
  dd_fetch_archive(
    "https://github.com/llvm/llvm-project/releases/download/llvmorg-${DOCS_DIGGER_LLVM_VERSION}/${LLVM_PACKAGE}.tar.xz"
    "8fb91cdc44fcbbdcf6b3ffd0a1f9859abd14a3c3aae4423c2b6d4a4f90bf0095"
    "${THIRDPARTY_DIR}" "${LLVM_PACKAGE}/lib/cmake/clang/ClangConfig.cmake")
  set(Clang_DIR "${THIRDPARTY_DIR}/${LLVM_PACKAGE}/lib/cmake/clang")
  set(LLVM_DIR "${THIRDPARTY_DIR}/${LLVM_PACKAGE}/lib/cmake/llvm")
endif()

find_package(Clang REQUIRED CONFIG)
message(STATUS "Using LLVM ${LLVM_PACKAGE_VERSION} from ${LLVM_DIR}")
message(STATUS "Using Clang from ${Clang_DIR}")

if(WIN32 AND NOT TARGET ZLIB::ZLIB)
  include(cmake/Compression.cmake)
endif()
include(cmake/Sqlite.cmake)

# overlunky checkout: tests use its sol2 headers; dig.bat configures it for the compile database.
# DD_OVERLUNKY_ROOT (environment) is the one setting both read; the default is the Playlunky submodule next door.
set(DD_DEFAULT_OVERLUNKY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../Playlunky/submodules/overlunky")
if(DEFINED ENV{DD_OVERLUNKY_ROOT})
  set(DD_DEFAULT_OVERLUNKY_ROOT "$ENV{DD_OVERLUNKY_ROOT}")
endif()
set(OVERLUNKY_ROOT "${DD_DEFAULT_OVERLUNKY_ROOT}" CACHE PATH "overlunky source root")

# --- docs-digger ------------------------------------------------------------------------------------------------
add_executable(docs-digger
  src/bindings.cpp
  src/bindings.hpp
  src/cache.cpp
  src/cache.hpp
  src/callback_join.cpp
  src/callbacks.cpp
  src/callbacks.hpp
  src/codec.cpp
  src/codec.hpp
  src/comments.cpp
  src/comments.hpp
  src/main.cpp
  src/merge.cpp
  src/merge.hpp
  src/model.cpp
  src/model.hpp
  src/resolve.cpp
  src/resolve.hpp
  src/runner.cpp
  src/runner.hpp
  src/source_root.cpp
  src/source_root.hpp
  src/types.cpp
  src/types.hpp
  src/usertypes.cpp
  src/usertypes.hpp)
target_include_directories(docs-digger SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(docs-digger PRIVATE ${LLVM_DEFINITIONS_LIST}
  # Clang's builtin headers (stddef.h, intrinsics) for the same version as the linked libraries.
  "DOCS_DIGGER_RESOURCE_DIR=\"${LLVM_LIBRARY_DIR}/clang/${LLVM_VERSION_MAJOR}\"")
target_link_libraries(docs-digger PRIVATE clangTooling clangASTMatchers clangFrontend clangAST clangBasic dd_sqlite)
# Match the LLVM libraries: no RTTI (classes derive from LLVM's), static runtime in every configuration because
# the package is Release-only (a Debug runtime or debug iterators would not link).
set_target_properties(docs-digger PROPERTIES MSVC_RUNTIME_LIBRARY MultiThreaded)
if(MSVC)
  target_compile_options(docs-digger PRIVATE /GR- /W4 /WX)
  # Deep template instantiation (sol2) overflows the 1 MB default stack; LLVM links its own tools the same way.
  target_link_options(docs-digger PRIVATE /STACK:10000000)
else()
  target_compile_options(docs-digger PRIVATE -fno-rtti -Wall -Wextra -Werror)
endif()

# --- Tests ------------------------------------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)

# --- Quality gate (quality.bat): clang-format and Lizard everywhere, clang-tidy only with a compile database --------
# check-* targets look at changed files and includers of changed headers; check-*-all at all of src/ (and tests/).
foreach(scope IN ITEMS changed all)
  set(suffix "")
  if(scope STREQUAL "all")
    set(suffix "-all")
  endif()
  add_custom_target(check-format${suffix}
    COMMAND "${CMAKE_COMMAND}" "-DQUALITY_SCOPE=${scope}" "-DQUALITY_BUILD_DIR=${PROJECT_BINARY_DIR}"
      -P "${PROJECT_SOURCE_DIR}/cmake/CheckFormat.cmake"
    COMMENT "Checking formatting with clang-format (${scope} files)"
    USES_TERMINAL VERBATIM)
  add_custom_target(check-complexity${suffix}
    COMMAND "${CMAKE_COMMAND}" "-DQUALITY_SCOPE=${scope}" "-DQUALITY_BUILD_DIR=${PROJECT_BINARY_DIR}"
      -P "${PROJECT_SOURCE_DIR}/cmake/CheckComplexity.cmake"
    COMMENT "Checking cyclomatic complexity with Lizard (${scope} files)"
    USES_TERMINAL VERBATIM)
  add_custom_target(check-quality${suffix})
  add_dependencies(check-quality${suffix} check-format${suffix} check-complexity${suffix})
  if(CMAKE_EXPORT_COMPILE_COMMANDS AND CMAKE_GENERATOR MATCHES "Ninja")
    add_custom_target(check-clang-tidy${suffix}
      COMMAND "${CMAKE_COMMAND}" "-DQUALITY_SCOPE=${scope}" "-DQUALITY_BUILD_DIR=${PROJECT_BINARY_DIR}"
        -P "${PROJECT_SOURCE_DIR}/cmake/CheckClangTidy.cmake"
      COMMENT "Checking diagnostics and cognitive complexity with clang-tidy (${scope} files)"
      USES_TERMINAL VERBATIM)
    add_dependencies(check-quality${suffix} check-clang-tidy${suffix})
  endif()
endforeach()
