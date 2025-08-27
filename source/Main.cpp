#include "Logger.h"
#include "Config.h"

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  reason,
	LPVOID lpReserved
)
{
	switch (reason)
	{
	case DLL_PROCESS_ATTACH:
		DisableThreadLibraryCalls(hModule);
		Logger::Init();
		Logger::Get()->info("AltairFix Loaded");
		LoadConfig();
		break;
	case DLL_THREAD_DETACH:
		Logger::Shutdown();
		break;
	default:
		break;
	}
	return true;
}