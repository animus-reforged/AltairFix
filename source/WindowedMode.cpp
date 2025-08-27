#include "WindowedMode.h"
#include "Logger.h"
#include "Config.h"
#include <imagehlp.h>
#include <sstream>

#pragma comment(lib, "ImageHlp.lib")

HWND WindowedMode::s_gameWindow = nullptr;
WindowedMode::tCreateWindowExA WindowedMode::s_TrueCreateWindowExA = nullptr;
WindowedMode::tClipCursor WindowedMode::s_TrueClipCursor = nullptr;

HWND WindowedMode::GetGameWindow()
{
	Logger::Get()->debug("GetGameWindow called, returning {}", (void*)s_gameWindow);
	return s_gameWindow;
}

// ---------------- Hooked ClipCursor ----------------
BOOL WINAPI WindowedMode::HookedClipCursor(const RECT* lpRect)
{
	std::ostringstream dbg;
	dbg << "HookedClipCursor called. lpRect=" << (lpRect ? "non-null" : "null");
	OutputDebugStringA(dbg.str().c_str());

	if (g_config.windowMode == 2 && s_gameWindow) {
		if (lpRect) {
			RECT windowRect;
			GetClientRect(s_gameWindow, &windowRect);

			// Convert top-left corner
			POINT topLeft = { windowRect.left, windowRect.top };
			ClientToScreen(s_gameWindow, &topLeft);

			// Convert bottom-right corner  
			POINT bottomRight = { windowRect.right, windowRect.bottom };
			ClientToScreen(s_gameWindow, &bottomRight);

			// Update the rect with screen coordinates
			windowRect.left = topLeft.x;
			windowRect.top = topLeft.y;
			windowRect.right = bottomRight.x;
			windowRect.bottom = bottomRight.y;

			Logger::Get()->info("ClipCursor adjusted to: {},{} - {},{}",
				windowRect.left, windowRect.top,
				windowRect.right, windowRect.bottom);

			return s_TrueClipCursor(&windowRect);
		}
		else {
			Logger::Get()->info("ClipCursor: released cursor clip");
			return s_TrueClipCursor(nullptr);
		}
	}

	Logger::Get()->debug("ClipCursor passthrough");
	return s_TrueClipCursor(lpRect);
}

// ---------------- Hooked CreateWindowExA ----------------
HWND WINAPI WindowedMode::HookedCreateWindowExA(
	DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName,
	DWORD dwStyle, int X, int Y, int nWidth, int nHeight,
	HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
{
	Logger::Get()->debug(
		"HookedCreateWindowExA called. class={}, title={}, pos=({},{}), size={}x{}",
		(lpClassName ? lpClassName : "null"),
		(lpWindowName ? lpWindowName : "null"),
		X, Y, nWidth, nHeight
	);

	if (lpClassName && strcmp(lpClassName, "ScimitarEngineWindowClass") == 0) {
		if (g_config.windowMode == 1) {
			Logger::Get()->info("Applying borderless mode");
			dwStyle &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZE | WS_MAXIMIZE | WS_SYSMENU);
			dwStyle |= WS_POPUP | WS_VISIBLE;
			X = Y = 0;
			nWidth = GetSystemMetrics(SM_CXSCREEN);
			nHeight = GetSystemMetrics(SM_CYSCREEN);
		}
		else if (g_config.windowMode == 2) {
			Logger::Get()->info("Applying windowed mode");
			dwStyle &= ~WS_POPUP;
			dwStyle |= WS_OVERLAPPEDWINDOW | WS_VISIBLE;

			if (g_config.windowWidth > 0 && g_config.windowHeight > 0) {
				nWidth = g_config.windowWidth;
				nHeight = g_config.windowHeight;
			}

			RECT adjusted = { 0,0,nWidth,nHeight };
			AdjustWindowRect(&adjusted, dwStyle, FALSE);
			int adjustedW = adjusted.right - adjusted.left;
			int adjustedH = adjusted.bottom - adjusted.top;

			if (g_config.windowPosX < 0 || g_config.windowPosY < 0) {
				X = (GetSystemMetrics(SM_CXSCREEN) - adjustedW) / 2;
				Y = (GetSystemMetrics(SM_CYSCREEN) - adjustedH) / 2;
			}
			else {
				X = g_config.windowPosX;
				Y = g_config.windowPosY;
			}

			nWidth = adjustedW;
			nHeight = adjustedH;
		}
	}

	HWND result = s_TrueCreateWindowExA(
		dwExStyle, lpClassName, lpWindowName, dwStyle,
		X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);

	if (lpClassName && strcmp(lpClassName, "ScimitarEngineWindowClass") == 0) {
		s_gameWindow = result;
		Logger::Get()->info("Stored game window handle: {}", (void*)result);
	}

	return result;
}

// ---------------- Hook Installation ----------------
void WindowedMode::Hook()
{
	Logger::Get()->debug("WindowedMode::Hook called");

	HMODULE hMod = GetModuleHandle(NULL);
	ULONG size;
	auto pDesc = (PIMAGE_IMPORT_DESCRIPTOR)ImageDirectoryEntryToData(
		hMod, TRUE, IMAGE_DIRECTORY_ENTRY_IMPORT, &size);

	if (!pDesc) {
		Logger::Get()->error("Failed to get IAT descriptor");
		return;
	}

	for (; pDesc->Name; pDesc++) {
		const char* dllName = (const char*)((BYTE*)hMod + pDesc->Name);
		Logger::Get()->debug("Checking imports from DLL: {}", dllName);

		if (_stricmp(dllName, "USER32.dll") == 0) {
			PIMAGE_THUNK_DATA thunk = (PIMAGE_THUNK_DATA)((BYTE*)hMod + pDesc->FirstThunk);

			for (; thunk->u1.Function; thunk++) {
				FARPROC* ppfn = (FARPROC*)&thunk->u1.Function;

				if (*ppfn == (FARPROC)CreateWindowExA) {
					DWORD oldProtect;
					VirtualProtect(ppfn, sizeof(void*), PAGE_READWRITE, &oldProtect);
					s_TrueCreateWindowExA = (tCreateWindowExA)*ppfn;
					*ppfn = (FARPROC)HookedCreateWindowExA;
					VirtualProtect(ppfn, sizeof(void*), oldProtect, &oldProtect);
					Logger::Get()->info("Hooked CreateWindowExA");
				}
				else if (*ppfn == (FARPROC)ClipCursor) {
					DWORD oldProtect;
					VirtualProtect(ppfn, sizeof(void*), PAGE_READWRITE, &oldProtect);
					s_TrueClipCursor = (tClipCursor)*ppfn;
					*ppfn = (FARPROC)HookedClipCursor;
					VirtualProtect(ppfn, sizeof(void*), oldProtect, &oldProtect);
					Logger::Get()->info("Hooked ClipCursor");
				}
			}
		}
	}

	Logger::Get()->debug("WindowedMode::Hook finished");
}