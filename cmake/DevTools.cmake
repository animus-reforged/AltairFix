# Development tooling targets (clang-format / clang-tidy).

find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format)
find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy)

# Collect project sources only; build/IDE/dependency directories are outside source/.
file(GLOB_RECURSE ALTAIRFIX_DEV_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/source/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/*.hh"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/*.hxx")

# clang-tidy can only analyze translation units, not standalone headers.
set(ALTAIRFIX_LINT_FILES ${ALTAIRFIX_DEV_FILES})
list(FILTER ALTAIRFIX_LINT_FILES EXCLUDE REGEX "\\.(h|hh|hpp|hxx)$")

if(CLANG_FORMAT_EXECUTABLE)
    add_custom_target(format
        COMMAND "${CLANG_FORMAT_EXECUTABLE}" --style=file -i ${ALTAIRFIX_DEV_FILES}
        COMMENT "Formatting C++ sources with clang-format"
        VERBATIM)

    add_custom_target(format-check
        COMMAND "${CLANG_FORMAT_EXECUTABLE}" --style=file --dry-run --Werror ${ALTAIRFIX_DEV_FILES}
        COMMENT "Checking C++ formatting with clang-format"
        VERBATIM)
else()
    message(STATUS "clang-format not found: 'format' and 'format-check' targets disabled")
endif()

if(CLANG_TIDY_EXECUTABLE)
    add_custom_target(lint
        COMMAND "${CLANG_TIDY_EXECUTABLE}"
                "--config-file=${CMAKE_CURRENT_SOURCE_DIR}/.clang-tidy"
                --warnings-as-errors=*
                -p "${CMAKE_BINARY_DIR}"
                ${ALTAIRFIX_LINT_FILES}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Running clang-tidy"
        USES_TERMINAL
        VERBATIM)
else()
    message(STATUS "clang-tidy not found: 'lint' target disabled")
endif()
