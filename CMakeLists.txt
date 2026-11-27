cmake_minimum_required(VERSION 4.4)
project(AltairFix LANGUAGES CXX)

if(NOT MINGW)
    message(FATAL_ERROR
        "AltairFix targets 32-bit MinGW-w64. Configure with the 'mingw32' preset "
        "(cmake --preset mingw32). In CLion, enable the 'MinGW 32-bit Debug' CMake profile.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
        "AltairFix requires a 32-bit build, but the selected compiler produces "
        "${CMAKE_SIZEOF_VOID_P}-byte pointers.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/GameEnvironment.cmake)

add_library(AltairFix SHARED source/dllmain.cpp)
target_include_directories(AltairFix PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/source")
target_link_libraries(AltairFix PRIVATE user32)

set_target_properties(AltairFix PROPERTIES
    PREFIX ""
    SUFFIX ".asi"
    RUNTIME_OUTPUT_DIRECTORY "${ALTAIRFIX_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${ALTAIRFIX_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${ALTAIRFIX_OUTPUT_DIR}"
)

include(cmake/DevTools.cmake)
