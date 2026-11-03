# SQLite for the per-translation-unit result cache (LLVM ships no database). The amalgamation is pinned in thirdparty/
# and built here as a static library with the static runtime the LLVM libraries use.
include("${CMAKE_CURRENT_LIST_DIR}/Download.cmake")

set(DD_SQLITE_DIR "${THIRDPARTY_DIR}/sqlite-amalgamation-3460100")
dd_fetch_archive("https://www.sqlite.org/2024/sqlite-amalgamation-3460100.zip"
  "77823cb110929c2bcb0f5d48e4833b5c59a8a6e40cdea3936b99e199dbbe5784" "${THIRDPARTY_DIR}"
  "sqlite-amalgamation-3460100/sqlite3.h")

add_library(dd_sqlite STATIC "${DD_SQLITE_DIR}/sqlite3.c")
# One connection, used under the digger's own lock; no extensions are loaded.
target_compile_definitions(dd_sqlite PRIVATE SQLITE_OMIT_LOAD_EXTENSION SQLITE_DEFAULT_MEMSTATUS=0)
target_include_directories(dd_sqlite SYSTEM INTERFACE "${DD_SQLITE_DIR}")
set_target_properties(dd_sqlite PROPERTIES MSVC_RUNTIME_LIBRARY MultiThreaded)
if(MSVC)
  target_compile_options(dd_sqlite PRIVATE /w)
else()
  target_compile_options(dd_sqlite PRIVATE -w)
endif()
if(NOT WIN32)
  target_link_libraries(dd_sqlite INTERFACE ${CMAKE_DL_LIBS})
endif()
