#pragma once
#include <windows.h>

class WindowedMode
{
public:
	static HWND GetGameWindow();
	static void Hook();

private:
	static HWND s_gameWindow;

	// Function pointers
	using tCreateWindowExA = HWND(WINAPI*)(DWORD, LPCSTR, LPCSTR, DWORD,
		int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
	using tClipCursor = BOOL(WINAPI*)(const RECT*);

	static tCreateWindowExA s_TrueCreateWindowExA;
	static tClipCursor s_TrueClipCursor;

	// Hooked implementations
	static BOOL WINAPI HookedClipCursor(const RECT* lpRect);
	static HWND WINAPI HookedCreateWindowExA(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName,
		DWORD dwStyle, int X, int Y, int nWidth, int nHeight,
		HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam);
};