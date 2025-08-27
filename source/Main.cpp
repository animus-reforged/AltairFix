#include "Logger.h"
#include "Config.h"
#include "WindowedMode.h"

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  reason,
	LPVOID lpReserved
)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(hModule);
		Logger::Init();
		Logger::Get()->info("AltairFix Loaded");
		LoadConfig();
		if (g_config.windowMode != 0)
		{
			WindowedMode::Hook();
		}
		else {
			Logger::Get()->info("WindowedMode not enabled in config");
		}
	}
	return true;
}