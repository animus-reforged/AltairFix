cmake_minimum_required(VERSION 4.1)
project(AltairFix)
set(CMAKE_CXX_STANDARD 20)

# Read .env file & extract DEBUG_OUTPUT
file(READ "${CMAKE_SOURCE_DIR}/.env" ENV_CONTENTS)
string(REGEX MATCH "DEBUG_OUTPUT=(.*)" _match "${ENV_CONTENTS}")
set(DEBUG_OUTPUT "${CMAKE_MATCH_1}")
message(STATUS "Debug scripts path from .env: ${DEBUG_OUTPUT}")

# Adding files
add_library(AltairFix SHARED src/main.cpp)

# Libraries
target_include_directories(AltairFix
        PRIVATE
        ${CMAKE_SOURCE_DIR}/external/spdlog/include # spdlog
        ${CMAKE_SOURCE_DIR}/external/minhook/include # MinHook
)

# Output
set_target_properties(AltairFix PROPERTIES
        PREFIX ""
        SUFFIX ".asi"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${DEBUG_OUTPUT}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/output"
)
