#include "ServerBlocker.h"
#include "Logger.h"
#include <cstring>

namespace ServerBlocker
{
	typedef void(__thiscall* FUN_t)(void* thisPtr, const char* param);
	static FUN_t fpOrig = nullptr;

	void __fastcall hkFUN(void* thisPtr, void* /*edx*/, const char* param)
	{
		if (param && strstr(param, "gconnect.ubi.com"))
		{
			Logger::Get()->warn("Blocked param: {}", param);
			return; // do nothing, skip building the hostname
		}

		// otherwise continue normal building
		fpOrig(thisPtr, param);
	}

	void Init(EngineType engine)
	{
		uintptr_t address = 0;
		switch (engine)
		{
		case ENGINE_DX9:
			address = 0x007C6140;
			break;
		case ENGINE_DX10:
			address = 0x00439640;
			break;
		default:
			Logger::Get()->error("Unknown engine type, cannot install patch");
			return;
		}

		Logger::Get()->info("Disabling gconnect.ubi.com for Function 0x{:X}", address);
		MH_CreateHook((LPVOID)address, &hkFUN, reinterpret_cast<LPVOID*>(&fpOrig));
		MH_EnableHook((LPVOID)address);
	}
}