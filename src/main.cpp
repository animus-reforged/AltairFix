#include <windows.h>
#include <iostream>

void Main()
{
    MessageBox(nullptr, "AltairFix DLL Loaded!", "Info", MB_OK);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        // Code to run when the DLL is loaded into a process
        DisableThreadLibraryCalls(hModule);
        Main();
        break;
    case DLL_PROCESS_DETACH:
        // Code to run when the DLL is unloaded from a process
        break;
    }
    return TRUE;
}
