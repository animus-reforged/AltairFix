# 32-bit MinGW-w64 toolchain.
# Requires i686-w64-mingw32-gcc / i686-w64-mingw32-g++ on PATH.
# Override with -DMINGW32_C_COMPILER=... and -DMINGW32_CXX_COMPILER=...

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86)

if(NOT MINGW32_C_COMPILER)
    find_program(MINGW32_C_COMPILER NAMES i686-w64-mingw32-gcc)
endif()
if(NOT MINGW32_C_COMPILER)
    # Some 32-bit-only MinGW distributions use unprefixed compiler names.
    find_program(_MINGW32_GCC NAMES gcc)
    if(_MINGW32_GCC)
        execute_process(COMMAND "${_MINGW32_GCC}" -dumpmachine
                        OUTPUT_VARIABLE _MINGW32_C_TARGET
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET)
        if(_MINGW32_C_TARGET MATCHES "^(i[3-6]86|x86)-")
            set(MINGW32_C_COMPILER "${_MINGW32_GCC}")
        endif()
    endif()
endif()

if(NOT MINGW32_CXX_COMPILER)
    find_program(MINGW32_CXX_COMPILER NAMES i686-w64-mingw32-g++)
endif()
if(NOT MINGW32_CXX_COMPILER)
    find_program(_MINGW32_GXX NAMES g++)
    if(_MINGW32_GXX)
        execute_process(COMMAND "${_MINGW32_GXX}" -dumpmachine
                        OUTPUT_VARIABLE _MINGW32_CXX_TARGET
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET)
        if(_MINGW32_CXX_TARGET MATCHES "^(i[3-6]86|x86)-")
            set(MINGW32_CXX_COMPILER "${_MINGW32_GXX}")
        endif()
    endif()
endif()

if(NOT MINGW32_C_COMPILER OR NOT MINGW32_CXX_COMPILER)
    message(FATAL_ERROR
        "32-bit MinGW-w64 compiler not found. Put i686-w64-mingw32-gcc and "
        "i686-w64-mingw32-g++ on PATH, or pass -DMINGW32_C_COMPILER=... and "
        "-DMINGW32_CXX_COMPILER=...")
endif()

set(CMAKE_C_COMPILER "${MINGW32_C_COMPILER}" CACHE FILEPATH "32-bit MinGW C compiler")
set(CMAKE_CXX_COMPILER "${MINGW32_CXX_COMPILER}" CACHE FILEPATH "32-bit MinGW C++ compiler")
