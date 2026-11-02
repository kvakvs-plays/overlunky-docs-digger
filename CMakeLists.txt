cmake_minimum_required(VERSION 3.20)
project(docs-digger CXX)

# C++23: complete enough in MSVC, clang-cl and GCC >= 14 / Clang >= 19 (Debian 13, Ubuntu 24.04 with g++-14,
# Ubuntu 26.04). C++26 is not yet complete in any stable toolchain.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Everything downloaded (LLVM, FetchContent sources, the analyzer venv) lives here, so build/ stays disposable.
set(THIRDPARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty")

# Prebuilt Clang/LLVM development package (static libs + CMake configs).
# On Windows it is downloaded once into thirdparty/ unless Clang_DIR is given.
# On Linux, Clang comes from distro packages or the Docker image (Clang_DIR / CMAKE_PREFIX_PATH).
set(DOCS_DIGGER_LLVM_VERSION "23.1.2")

if(WIN32 AND NOT Clang_DIR)
  set(LLVM_PACKAGE "clang+llvm-${DOCS_DIGGER_LLVM_VERSION}-x86_64-pc-windows-msvc")
  set(LLVM_ARCHIVE_SHA256 "8fb91cdc44fcbbdcf6b3ffd0a1f9859abd14a3c3aae4423c2b6d4a4f90bf0095")
  set(LLVM_ROOT_DIR "${THIRDPARTY_DIR}/${LLVM_PACKAGE}")

  if(NOT EXISTS "${LLVM_ROOT_DIR}/lib/cmake/clang/ClangConfig.cmake")
    set(LLVM_ARCHIVE "${THIRDPARTY_DIR}/${LLVM_PACKAGE}.tar.xz")
    if(NOT EXISTS "${LLVM_ARCHIVE}")
      message(STATUS "Downloading ${LLVM_PACKAGE}.tar.xz (~860 MB)...")
      file(DOWNLOAD
        "https://github.com/llvm/llvm-project/releases/download/llvmorg-${DOCS_DIGGER_LLVM_VERSION}/${LLVM_PACKAGE}.tar.xz"
        "${LLVM_ARCHIVE}.part"
        EXPECTED_HASH SHA256=${LLVM_ARCHIVE_SHA256}
        SHOW_PROGRESS
        STATUS DOWNLOAD_STATUS)
      list(GET DOWNLOAD_STATUS 0 DOWNLOAD_CODE)
      if(NOT DOWNLOAD_CODE EQUAL 0)
        file(REMOVE "${LLVM_ARCHIVE}.part")
        message(FATAL_ERROR "Downloading ${LLVM_PACKAGE} failed: ${DOWNLOAD_STATUS}")
      endif()
      file(RENAME "${LLVM_ARCHIVE}.part" "${LLVM_ARCHIVE}")
    endif()

    message(STATUS "Unpacking ${LLVM_PACKAGE}...")
    file(ARCHIVE_EXTRACT INPUT "${LLVM_ARCHIVE}" DESTINATION "${THIRDPARTY_DIR}")
    if(NOT EXISTS "${LLVM_ROOT_DIR}/lib/cmake/clang/ClangConfig.cmake")
      message(FATAL_ERROR "${LLVM_PACKAGE} has no lib/cmake/clang/ClangConfig.cmake after unpacking")
    endif()
    file(REMOVE "${LLVM_ARCHIVE}")
  endif()

  set(Clang_DIR "${LLVM_ROOT_DIR}/lib/cmake/clang")
  set(LLVM_DIR "${LLVM_ROOT_DIR}/lib/cmake/llvm")
endif()

find_package(Clang REQUIRED CONFIG)
message(STATUS "Using LLVM ${LLVM_PACKAGE_VERSION} from ${LLVM_DIR}")
message(STATUS "Using Clang from ${Clang_DIR}")

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
