#include "Engine.h"

EngineType DetectEngine()
{
    Logger::Get()->info("Detecting engine...");

    __try
    {
        if (MEMCMP32(0x00401375 + 1, 0x42d6))
        {
            return ENGINE_DX9;
        }
        else if (MEMCMP32(0x004013DE + 1, 0x428d))
        {
            return ENGINE_DX10;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Logger::Get()->error("Exception while checking engine signatures");
    }

    return ENGINE_UNKNOWN;
}
