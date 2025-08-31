#include "Logger.h"
#include "Config.h"
#include "WindowedMode.h"
#include "MinHook.h"

#define MEMCMP32(addr, val) (*(DWORD*)(addr) == (DWORD)(val))

enum EngineType
{
	ENGINE_UNKNOWN,
	ENGINE_DX9,
	ENGINE_DX10
};

static EngineType DetectEngine()
{
	Logger::Get()->info("Detecting engine...");
	__try {
		if (MEMCMP32(0x00401375 + 1, 0x42d6))
		{
			return ENGINE_DX9;
		}
		else if (MEMCMP32(0x004013DE + 1, 0x428d)) {
			return ENGINE_DX10;
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		Logger::Get()->error("Exception while checking engine signatures");
	}
	return ENGINE_UNKNOWN;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
	switch (reason)
	{
	case DLL_PROCESS_ATTACH:
	{
		DisableThreadLibraryCalls(hModule);
		Logger::Init();

		LoadConfig();
		Logger::Get()->info("Config: WindowMode={}, Size={}x{}, PosX={}, PosY={}",
			g_config.windowMode, g_config.windowWidth, g_config.windowHeight, g_config.windowPosX, g_config.windowPosY);

		if (g_config.windowMode != 0)
		{
			if (MH_Initialize() != MH_OK) {
				Logger::Get()->error("MinHook init failed"); break;
			}
			EngineType engine = DetectEngine();
			switch (engine)
			{
			case ENGINE_DX9:
				Logger::Get()->info("Engine=DX9");
				WindowedMode::HookD3D9();
				break;
			case ENGINE_DX10:
				Logger::Get()->info("Engine=DX10");
				WindowedMode::HookD3D10(); break;
			default:          Logger::Get()->warn("Engine=Unknown");
				break;
			}
			WindowedMode::HookFocusFunctions();
			MH_EnableHook(MH_ALL_HOOKS);
			Logger::Get()->info("Hooks installed for detected engine");
		}
	}
	break;

	case DLL_PROCESS_DETACH:
		if (g_config.windowMode != 0)
		{
			Logger::Get()->info("AltairFix unloading, removing hooks...");
			WindowedMode::Unhook();
			MH_Uninitialize();
		}
		break;
	}
	return TRUE;
}