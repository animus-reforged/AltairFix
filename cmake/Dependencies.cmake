# Third-party dependencies.
#
# Libraries live under external/ as Git submodules, so a fresh clone needs no
# system packages, package manager, or manual downloads. After cloning (or when
# switching to a branch that adds a dependency), fetch them with:
#
#     git submodule update --init --recursive

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/spdlog/CMakeLists.txt")
    message(FATAL_ERROR
        "The spdlog submodule is missing.\n"
        "Fetch it with: git submodule update --init --recursive")
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/tomlplusplus/CMakeLists.txt")
    message(FATAL_ERROR
        "The tomlplusplus submodule is missing.\n"
        "Fetch it with: git submodule update --init --recursive")
endif()

# spdlog is the diagnostic logging backend. It is compiled as a static library
# and linked into AltairFix.asi, keeping the mod a single-file deployment with
# no extra runtime DLLs to install.
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "AltairFix: do not build spdlog examples" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "AltairFix: do not build spdlog tests" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "AltairFix: do not build spdlog benchmarks" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "AltairFix: do not generate spdlog install rules" FORCE)

# Game installations can live under paths with non-ASCII characters (for
# example, a localized user profile). Wide filenames let spdlog open
# "AltairFix.log" next to the ASI regardless of the active ANSI code page.
set(SPDLOG_WCHAR_FILENAMES ON CACHE BOOL "AltairFix: support Unicode log paths" FORCE)

# Treat spdlog headers as system headers so compiler warnings and clang-tidy
# diagnostics stay focused on AltairFix sources.
set(SPDLOG_SYSTEM_INCLUDES ON CACHE BOOL "AltairFix: include spdlog as a system header" FORCE)

add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/external/spdlog"
    "${CMAKE_CURRENT_BINARY_DIR}/external/spdlog"
    EXCLUDE_FROM_ALL)

# toml++ parses AltairFix.toml. It is consumed header-only, so there is no
# dependency library to link; only the interface target is needed.
set(tomlplusplus_BUILD_LIB OFF CACHE BOOL "AltairFix: consume toml++ header-only" FORCE)
set(tomlplusplus_INSTALL OFF CACHE BOOL "AltairFix: do not generate toml++ install rules" FORCE)
set(tomlplusplus_INCLUDES_WITH_SYSTEM ON CACHE BOOL "AltairFix: include toml++ as a system header" FORCE)

add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/external/tomlplusplus"
    "${CMAKE_CURRENT_BINARY_DIR}/external/tomlplusplus"
    EXCLUDE_FROM_ALL)
