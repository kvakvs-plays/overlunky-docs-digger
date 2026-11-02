cmake_minimum_required(VERSION 3.20)
project(docs-digger CXX)

set(CMAKE_CXX_STANDARD 20)

# Prebuilt Clang/LLVM development package (static libs + CMake configs).
# On Windows it is downloaded once into thirdparty/ unless Clang_DIR is given.
# On Linux, Clang comes from distro packages or the Docker image (Clang_DIR / CMAKE_PREFIX_PATH).
set(DOCS_DIGGER_LLVM_VERSION "23.1.2")

if(WIN32 AND NOT Clang_DIR)
  set(LLVM_PACKAGE "clang+llvm-${DOCS_DIGGER_LLVM_VERSION}-x86_64-pc-windows-msvc")
  set(LLVM_ARCHIVE_SHA256 "8fb91cdc44fcbbdcf6b3ffd0a1f9859abd14a3c3aae4423c2b6d4a4f90bf0095")
  set(THIRDPARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty")
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
