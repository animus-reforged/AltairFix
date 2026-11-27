#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

namespace
{
    DWORD WINAPI ShowInjectedMessage(LPVOID /*unused*/)
    {
        MessageBoxW(nullptr,
                    L"AltairFix injected successfully!\n\nThis is the placeholder mod entry point.",
                    L"AltairFix",
                    MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND | MB_TOPMOST);
        return 0;
    }
} // namespace

extern "C" BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID /*reserved*/)
{
    if (reason != DLL_PROCESS_ATTACH)
    {
        return TRUE;
    }

    DisableThreadLibraryCalls(module);

    HANDLE thread = CreateThread(nullptr, 0, ShowInjectedMessage, nullptr, 0, nullptr);
    if (thread != nullptr)
    {
        CloseHandle(thread);
    }

    return TRUE;
}