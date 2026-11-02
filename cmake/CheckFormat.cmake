# Formatting gate: clang-format --dry-run over the C++ sources in src/ and tests/ (cmake -P, see QualityCommon.cmake).
# tests/fixtures/ is skipped: fixtures imitate overlunky sources and golden outputs depend on their line numbers.
include("${CMAKE_CURRENT_LIST_DIR}/QualityCommon.cmake")

if(QUALITY_SCOPE STREQUAL "changed")
    dd_quality_scope(format inputs)
else()
    file(GLOB_RECURSE inputs RELATIVE "${project_root}"
        "${project_root}/src/*.cpp" "${project_root}/src/*.hpp" "${project_root}/src/*.h" "${project_root}/src/*.inl"
        "${project_root}/tests/*.cpp" "${project_root}/tests/*.hpp" "${project_root}/tests/*.h")
    list(FILTER inputs EXCLUDE REGEX "^tests/fixtures/")
endif()
if(NOT inputs)
    message(STATUS "No C++ files to check in src/ or tests/; clang-format skipped.")
    return()
endif()

get_filename_component(venv_scripts "${QUALITY_PYTHON}" DIRECTORY)
find_program(CLANG_FORMAT NAMES clang-format HINTS "${venv_scripts}" NO_DEFAULT_PATH REQUIRED)
execute_process(
    COMMAND "${CLANG_FORMAT}" --dry-run --Werror --style=file ${inputs}
    WORKING_DIRECTORY "${project_root}"
    RESULT_VARIABLE format_result)
if(NOT format_result STREQUAL "0")
    message(FATAL_ERROR "Formatting check failed. Run: thirdparty\\venv-quality\\Scripts\\clang-format.exe -i --style=file <files>")
endif()
message(STATUS "clang-format passed (scope ${QUALITY_SCOPE}).")
