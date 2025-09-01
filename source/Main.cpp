#include "MinHook.h"
#include "Logger.h"
#include "Config.h"
#include "CpuLimiter.h"
#include "WindowedMode.h"
#include "Engine.h"
#include "ServerBlocker.h"


BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
	switch (reason)
	{
	case DLL_PROCESS_ATTACH:
	{
		DisableThreadLibraryCalls(hModule);
		Logger::Init();

		LoadConfig();
		Logger::Get()->info("----------");
		Logger::Get()->info("{} v{} loaded.", Constants::FixName, g_config.version);
		Logger::Get()->info("Log file: {}", Constants::FullLogFilePath.string());
		Logger::Get()->info("----------");
		Logger::Get()->info("Config: WindowMode={}, Size={}x{}, PosX={}, PosY={}",
			g_config.windowMode, g_config.windowWidth, g_config.windowHeight, g_config.windowPosX, g_config.windowPosY);
		if (MH_Initialize() != MH_OK) {
			Logger::Get()->error("MinHook init failed");
			break;
		}
		EngineType engine = DetectEngine();
		if (g_config.highCoreCountFix)
		{
			CpuLimiter::ProcessAffinityLimiter();
		}

		if (g_config.serverBlocker)
		{
			Logger::Get()->info("Enabling server blocker");
			ServerBlocker::Init(engine);
		}

		if (g_config.windowMode != 0)
		{
			WindowedMode::Hook(engine);
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