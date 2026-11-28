/// @file D3d10Shim.cpp
/// @brief Small DLL that makes the Ubisoft DX selector believe D3D10 is absent.
///
/// AssassinsCreed_Game.exe decides the renderer by calling
/// LoadLibraryW(L"d3d10.dll"). It only checks whether the load succeeds; it does
/// not inspect the resolved function pointers. Placing this DLL next to the game
/// executable makes that load fail, so the selector falls back to DX9 without
/// patching either executable.
///
/// The shim is deliberately not linked into AltairFix.asi and does not affect
/// the game itself, which uses d3d9.dll.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

/// @brief Fails DLL_PROCESS_ATTACH so LoadLibraryW reports the DLL as missing.
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) // NOLINT(misc-use-internal-linkage) - PE entry point needs external linkage.
{
    (void)module;
    (void)reserved;
    return reason == DLL_PROCESS_ATTACH ? FALSE : TRUE;
}
