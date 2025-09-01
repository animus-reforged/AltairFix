#pragma once
#include "Logger.h"

#define MEMCMP32(addr, val) (*(DWORD*)(addr) == (DWORD)(val))

enum EngineType
{
    ENGINE_UNKNOWN,
    ENGINE_DX9,
    ENGINE_DX10
};

// Detect the engine type at runtime
EngineType DetectEngine();