#include "MinHook.h"
#include "Logger.h"
#include "Config.h"
#include "CpuLimiter.h"
#include "WindowedMode.h"
#include "Engine.h"


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

		if (g_config.highCoreCountFix)
		{
			CpuLimiter::ProcessAffinityLimiter();
		}

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