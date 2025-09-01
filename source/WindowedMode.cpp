#include "WindowedMode.h"
#include "Logger.h"
#include "Config.h"
#include "MinHook.h"

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3d10.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "libMinHook.x86.lib")

// Static member initialization
HWND WindowedMode::s_gameWindow = nullptr;
bool WindowedMode::s_blockFocusLoss = false;
WNDPROC WindowedMode::s_OriginalWndProc = nullptr;

// D3D9 state
void** WindowedMode::s_pD3D9VTable = nullptr;
void** WindowedMode::s_pDeviceVTable = nullptr;
IDirect3DDevice9* WindowedMode::s_pDevice = nullptr;

// D3D10/DXGI state
IDXGISwapChain* WindowedMode::s_pSwapChain = nullptr;
ID3D10Device* WindowedMode::s_pDevice10 = nullptr;
HRESULT(STDMETHODCALLTYPE* WindowedMode::s_TruePresent10)(IDXGISwapChain*, UINT, UINT) = nullptr;

// D3D9 function pointers
HRESULT(WINAPI* WindowedMode::s_TrueCreateDevice)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**) = nullptr;
HRESULT(WINAPI* WindowedMode::s_TrueReset)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*) = nullptr;
HRESULT(WINAPI* WindowedMode::s_TruePresent)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*) = nullptr;

// D3D10/DXGI function pointers
HRESULT(WINAPI* WindowedMode::s_TrueD3D10CreateDevice)(IDXGIAdapter*, D3D10_DRIVER_TYPE, HMODULE, UINT, UINT, ID3D10Device**) = nullptr;
HRESULT(STDMETHODCALLTYPE* WindowedMode::s_TrueCreateSwapChain)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**) = nullptr;
HRESULT(WINAPI* WindowedMode::s_TrueD3D10CreateDeviceAndSwapChain)(
	IDXGIAdapter*, D3D10_DRIVER_TYPE, HMODULE, UINT, UINT,
	DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**, ID3D10Device**) = nullptr;

// Focus function pointers
BOOL(WINAPI* WindowedMode::s_TrueGetMessage)(LPMSG, HWND, UINT, UINT) = nullptr;
BOOL(WINAPI* WindowedMode::s_TruePeekMessage)(LPMSG, HWND, UINT, UINT, UINT) = nullptr;
HWND(WINAPI* WindowedMode::s_TrueGetForegroundWindow)() = nullptr;
HWND(WINAPI* WindowedMode::s_TrueGetFocus)() = nullptr;
SHORT(WINAPI* WindowedMode::s_TrueGetAsyncKeyState)(int) = nullptr;

// DXGI Factory hook helper
typedef HRESULT(WINAPI* PFN_CreateDXGIFactory)(REFIID riid, void** ppFactory);
static PFN_CreateDXGIFactory s_TrueCreateDXGIFactory = nullptr;

//=============================================================================
// Public Methods
//=============================================================================

HWND WindowedMode::GetGameWindow()
{
	return s_gameWindow;
}

void WindowedMode::Hook(EngineType engine)
{
	switch (engine)
	{
	case ENGINE_DX9:
		Logger::Get()->info("Engine=DX9");
		HookD3D9();
		break;
	case ENGINE_DX10:
		Logger::Get()->info("Engine=DX10");
		HookD3D10();
		break;
	default:
		Logger::Get()->warn("Engine=Unknown");
		return;
	}
	HookFocusFunctions();
	MH_EnableHook(MH_ALL_HOOKS);

	Logger::Get()->info("All WindowedMode hooks enabled");
}

void WindowedMode::Unhook()
{
	if (s_OriginalWndProc && s_gameWindow)
	{
		SetWindowLongPtr(s_gameWindow, GWLP_WNDPROC, (LONG_PTR)s_OriginalWndProc);
	}

	MH_Uninitialize();
	Logger::Get()->info("Hooks removed and MinHook uninitialized");
}

//=============================================================================
// Engine-specific Hook Installation
//=============================================================================

void WindowedMode::HookD3D9()
{
	IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
	if (!d3d)
	{
		Logger::Get()->error("Failed to Direct3DCreate9");
		return;
	}

	s_pD3D9VTable = *reinterpret_cast<void***>(d3d);
	MH_CreateHook(s_pD3D9VTable[16], &HookedCreateDevice, (void**)&s_TrueCreateDevice);
	MH_EnableHook(s_pD3D9VTable[16]);

	Logger::Get()->info("D3D9 CreateDevice hook installed");
	d3d->Release();
}

void WindowedMode::HookD3D10()
{
	// Hook DXGI Factory first
	HMODULE hDXGI = GetModuleHandleW(L"dxgi.dll");
	if (!hDXGI)
	{
		hDXGI = LoadLibraryW(L"dxgi.dll");
	}

	if (hDXGI)
	{
		auto pFunc = (PFN_CreateDXGIFactory)GetProcAddress(hDXGI, "CreateDXGIFactory");
		if (pFunc)
		{
			MH_CreateHook(pFunc, &HookedCreateDXGIFactory, (void**)&s_TrueCreateDXGIFactory);
			Logger::Get()->info("Hooked CreateDXGIFactory");
		}
	}

	// Hook D3D10 CreateDevice variants
	HMODULE hD3D10 = GetModuleHandleW(L"d3d10.dll");
	if (!hD3D10)
	{
		hD3D10 = LoadLibraryW(L"d3d10.dll");
	}

	if (hD3D10)
	{
		// Hook the main CreateDevice function
		void* addr = GetProcAddress(hD3D10, "D3D10CreateDevice");
		if (addr)
		{
			MH_CreateHook(addr, &HookedD3D10CreateDevice, (void**)&s_TrueD3D10CreateDevice);
			Logger::Get()->info("Hooked D3D10CreateDevice");
		}

		// Hook D3D10CreateDeviceAndSwapChain
		void* addrWithSwap = GetProcAddress(hD3D10, "D3D10CreateDeviceAndSwapChain");
		if (addrWithSwap)
		{
			MH_CreateHook(addrWithSwap, &HookedD3D10CreateDeviceAndSwapChain, (void**)&s_TrueD3D10CreateDeviceAndSwapChain);
			Logger::Get()->info("Hooked D3D10CreateDeviceAndSwapChain");
		}
	}
}

void WindowedMode::HookFocusFunctions()
{
	MH_CreateHookApi(L"user32", "GetMessageA", &HookedGetMessage, (void**)&s_TrueGetMessage);
	MH_CreateHookApi(L"user32", "PeekMessageA", &HookedPeekMessage, (void**)&s_TruePeekMessage);
	MH_CreateHookApi(L"user32", "GetForegroundWindow", &HookedGetForegroundWindow, (void**)&s_TrueGetForegroundWindow);
	MH_CreateHookApi(L"user32", "GetFocus", &HookedGetFocus, (void**)&s_TrueGetFocus);
	MH_CreateHookApi(L"user32", "GetAsyncKeyState", &HookedGetAsyncKeyState, (void**)&s_TrueGetAsyncKeyState);

	Logger::Get()->info("Focus hooks installed");
}

//=============================================================================
// Helper Functions
//=============================================================================

void WindowedMode::ConfineCursorToWindow(HWND hWnd)
{
	if (!hWnd) return;

	RECT rect;
	GetClientRect(hWnd, &rect);

	POINT tl = { rect.left, rect.top };
	POINT br = { rect.right, rect.bottom };
	ClientToScreen(hWnd, &tl);
	ClientToScreen(hWnd, &br);

	rect.left = tl.x;
	rect.top = tl.y;
	rect.right = br.x;
	rect.bottom = br.y;

	Logger::Get()->debug("ConfineCursorToWindow: RECT l={} t={} r={} b={}",
		rect.left, rect.top, rect.right, rect.bottom);
	ClipCursor(&rect);
}

void WindowedMode::FilterMessage(LPMSG lpMsg)
{
	if (!lpMsg || !s_gameWindow || !s_blockFocusLoss) return;

	if (lpMsg->hwnd != s_gameWindow) return;

	Logger::Get()->debug("FilterMessage: intercept HWND=0x{:X}, Msg=0x{:X}",
		(uintptr_t)lpMsg->hwnd, lpMsg->message);

	switch (lpMsg->message)
	{
	case WM_KILLFOCUS:
	case WM_ACTIVATEAPP:
		lpMsg->message = WM_NULL;
		Logger::Get()->debug("Suppressed focus loss message");
		break;

	case WM_ACTIVATE:
		if (LOWORD(lpMsg->wParam) == WA_INACTIVE)
		{
			Logger::Get()->debug("Changing WM_ACTIVATE inactive -> active");
			lpMsg->wParam = MAKEWPARAM(WA_ACTIVE, HIWORD(lpMsg->wParam));
		}
		break;

	case WM_NCACTIVATE:
		if (lpMsg->wParam == FALSE)
		{
			Logger::Get()->debug("Suppressing WM_NCACTIVATE FALSE -> TRUE");
			lpMsg->wParam = TRUE;
		}
		break;
	}
}

void WindowedMode::AdjustPresentParameters(D3DPRESENT_PARAMETERS* params)
{
	if (!params) return;

	Logger::Get()->debug("AdjustPresentParameters: before {}x{}, Windowed={}",
		params->BackBufferWidth, params->BackBufferHeight, params->Windowed);

	if (g_config.windowMode == 1) // Borderless
	{
		params->Windowed = TRUE;
		params->FullScreen_RefreshRateInHz = 0;
		params->BackBufferWidth = GetSystemMetrics(SM_CXSCREEN);
		params->BackBufferHeight = GetSystemMetrics(SM_CYSCREEN);

		Logger::Get()->info("Adjusted DX9 Present Params → Borderless ({}x{})",
			params->BackBufferWidth, params->BackBufferHeight);
	}
	else if (g_config.windowMode == 2) // Windowed
	{
		params->Windowed = TRUE;
		params->FullScreen_RefreshRateInHz = 0;

		if (g_config.windowWidth > 0)
			params->BackBufferWidth = g_config.windowWidth;
		if (g_config.windowHeight > 0)
			params->BackBufferHeight = g_config.windowHeight;

		Logger::Get()->info("Adjusted DX9 Present Params → Windowed ({}x{})",
			params->BackBufferWidth, params->BackBufferHeight);
	}
}

void WindowedMode::AdjustSwapChainDesc(DXGI_SWAP_CHAIN_DESC* desc)
{
	if (!desc) return;

	Logger::Get()->debug("AdjustSwapChainDesc: before {}x{}, windowed={}, hwnd=0x{:X}",
		desc->BufferDesc.Width, desc->BufferDesc.Height, desc->Windowed, (uintptr_t)desc->OutputWindow);

	// Force windowed mode
	desc->Windowed = TRUE;
	desc->BufferDesc.RefreshRate.Numerator = 0;
	desc->BufferDesc.RefreshRate.Denominator = 0;

	// Ensure we have a valid window handle
	if (!desc->OutputWindow)
	{
		Logger::Get()->warn("No OutputWindow in swap chain desc");
		return;
	}

	if (g_config.windowMode == 1) // Borderless
	{
		int screenWidth = GetSystemMetrics(SM_CXSCREEN);
		int screenHeight = GetSystemMetrics(SM_CYSCREEN);

		Logger::Get()->info("AdjustSwapChainDesc → Borderless {}x{}", screenWidth, screenHeight);

		// Set buffer size to screen size for borderless
		desc->BufferDesc.Width = screenWidth;
		desc->BufferDesc.Height = screenHeight;
	}
	else if (g_config.windowMode == 2) // Windowed
	{
		int clientWidth = g_config.windowWidth > 0 ? g_config.windowWidth : 1280;
		int clientHeight = g_config.windowHeight > 0 ? g_config.windowHeight : 720;

		Logger::Get()->info("AdjustSwapChainDesc → Windowed {}x{}", clientWidth, clientHeight);

		// Set buffer size to desired client area
		desc->BufferDesc.Width = clientWidth;
		desc->BufferDesc.Height = clientHeight;
	}
}

void WindowedMode::SetupWindow(HWND hWnd)
{
	if (!hWnd || hWnd == s_gameWindow) return;

	Logger::Get()->info("SetupWindow: HWND=0x{:X}, mode={}", (uintptr_t)hWnd, g_config.windowMode);

	s_gameWindow = hWnd;
	s_blockFocusLoss = (g_config.windowMode == 1 || g_config.windowMode == 2);

	if (g_config.windowMode == 1) // Borderless
	{
		Logger::Get()->info("Applying borderless style");

		LONG style = GetWindowLong(hWnd, GWL_STYLE);
		style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZE | WS_MAXIMIZE | WS_SYSMENU);
		style |= WS_POPUP;
		SetWindowLong(hWnd, GWL_STYLE, style);

		SetWindowPos(hWnd, HWND_TOP, 0, 0,
			GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
			SWP_FRAMECHANGED | SWP_SHOWWINDOW);
	}
	else if (g_config.windowMode == 2) // Windowed
	{
		Logger::Get()->info("Applying normal windowed style");

		LONG style = GetWindowLong(hWnd, GWL_STYLE);
		style &= ~WS_POPUP;
		style |= WS_OVERLAPPEDWINDOW;
		SetWindowLong(hWnd, GWL_STYLE, style);

		int width = g_config.windowWidth > 0 ? g_config.windowWidth : 1280;
		int height = g_config.windowHeight > 0 ? g_config.windowHeight : 720;

		RECT rect = { 0, 0, width, height };
		AdjustWindowRect(&rect, style, FALSE);

		int winWidth = rect.right - rect.left;
		int winHeight = rect.bottom - rect.top;
		int x = (GetSystemMetrics(SM_CXSCREEN) - winWidth) / 2;
		int y = (GetSystemMetrics(SM_CYSCREEN) - winHeight) / 2;

		if (g_config.windowPosX >= 0) x = g_config.windowPosX;
		if (g_config.windowPosY >= 0) y = g_config.windowPosY;

		SetWindowPos(hWnd, HWND_TOP, x, y, winWidth, winHeight,
			SWP_FRAMECHANGED | SWP_SHOWWINDOW);
	}

	// Hook window procedure
	s_OriginalWndProc = (WNDPROC)SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)WindowProc);
	Logger::Get()->debug("WndProc hooked (original at 0x{:X})", (uintptr_t)s_OriginalWndProc);
}

//=============================================================================
// Window Procedure
//=============================================================================

LRESULT CALLBACK WindowedMode::WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	Logger::Get()->trace("WndProc: Msg=0x{:X}", msg);

	switch (msg)
	{
	case WM_ACTIVATE:
		if (LOWORD(wParam) != WA_INACTIVE)
			ConfineCursorToWindow(hWnd);
		else
			ClipCursor(nullptr);
		break;

	case WM_KILLFOCUS:
		ClipCursor(nullptr);
		break;

	case WM_SETFOCUS:
		ConfineCursorToWindow(hWnd);
		break;

	case WM_SETCURSOR:
		if (GetForegroundWindow() == hWnd)
			ConfineCursorToWindow(hWnd);
		break;
	}

	return CallWindowProc(s_OriginalWndProc, hWnd, msg, wParam, lParam);
}

//=============================================================================
// D3D9 Hooks
//=============================================================================

HRESULT WINAPI WindowedMode::HookedCreateDevice(
	IDirect3D9* self, UINT adapter, D3DDEVTYPE type,
	HWND win, DWORD flags, D3DPRESENT_PARAMETERS* p,
	IDirect3DDevice9** outDev)
{
	Logger::Get()->info("HookedCreateDevice called (adapter={}, hwnd=0x{:X})",
		adapter, (uintptr_t)win);

	AdjustPresentParameters(p);
	SetupWindow(win);

	HRESULT hr = s_TrueCreateDevice(self, adapter, type, win, flags, p, outDev);

	if (SUCCEEDED(hr) && outDev && *outDev)
	{
		Logger::Get()->info("DX9 device created successfully, hooking Reset/Present");

		s_pDevice = *outDev;
		s_pDeviceVTable = *reinterpret_cast<void***>(s_pDevice);

		if (!s_TrueReset)
		{
			MH_CreateHook(s_pDeviceVTable[16], &HookedReset, (void**)&s_TrueReset);
			MH_EnableHook(s_pDeviceVTable[16]);
			Logger::Get()->debug("DX9 Reset hook installed");
		}

		if (!s_TruePresent)
		{
			MH_CreateHook(s_pDeviceVTable[17], &HookedPresent, (void**)&s_TruePresent);
			MH_EnableHook(s_pDeviceVTable[17]);
			Logger::Get()->debug("DX9 Present hook installed");
		}
	}

	return hr;
}

HRESULT WINAPI WindowedMode::HookedReset(IDirect3DDevice9* self, D3DPRESENT_PARAMETERS* params)
{
	Logger::Get()->debug("HookedReset called");
	AdjustPresentParameters(params);
	return s_TrueReset(self, params);
}

HRESULT WINAPI WindowedMode::HookedPresent(
	IDirect3DDevice9* self, const RECT* src, const RECT* dst,
	HWND wnd, const RGNDATA* dirty)
{
	return s_TruePresent(self, src, dst, wnd, dirty);
}

//=============================================================================
// D3D10/DXGI Hooks
//=============================================================================

HRESULT WINAPI WindowedMode::HookedD3D10CreateDevice(
	IDXGIAdapter* pAdapter, D3D10_DRIVER_TYPE DriverType,
	HMODULE Software, UINT Flags, UINT SDKVersion,
	ID3D10Device** ppDevice)
{
	Logger::Get()->info("HookedD3D10CreateDevice");

	HRESULT hr = s_TrueD3D10CreateDevice(pAdapter, DriverType, Software, Flags, SDKVersion, ppDevice);

	if (SUCCEEDED(hr) && ppDevice && *ppDevice)
	{
		s_pDevice10 = *ppDevice;
		Logger::Get()->info("DX10 device created OK");
	}

	return hr;
}

HRESULT STDMETHODCALLTYPE WindowedMode::HookedCreateSwapChain(
	IDXGIFactory* self, IUnknown* pDevice,
	DXGI_SWAP_CHAIN_DESC* pDesc, IDXGISwapChain** ppSwapChain)
{
	Logger::Get()->info("HookedCreateSwapChain called");

	if (!pDesc)
	{
		Logger::Get()->error("Null swap chain descriptor");
		return s_TrueCreateSwapChain(self, pDevice, pDesc, ppSwapChain);
	}

	// Store original values for fallback
	DXGI_SWAP_CHAIN_DESC originalDesc = *pDesc;

	// Apply our modifications
	AdjustSwapChainDesc(pDesc);

	// Set up the window BEFORE creating the swap chain
	if (pDesc->OutputWindow)
	{
		SetupWindow(pDesc->OutputWindow);
	}

	HRESULT hr = s_TrueCreateSwapChain(self, pDevice, pDesc, ppSwapChain);

	if (SUCCEEDED(hr) && ppSwapChain && *ppSwapChain)
	{
		s_pSwapChain = *ppSwapChain;

		// Hook Present on the swap chain
		void** vtbl = *reinterpret_cast<void***>(s_pSwapChain);
		if (!s_TruePresent10)
		{
			MH_CreateHook(vtbl[8], &HookedPresent10, (void**)&s_TruePresent10);
			MH_EnableHook(vtbl[8]);
			Logger::Get()->info("Hooked IDXGISwapChain::Present");
		}

		Logger::Get()->info("DXGI SwapChain created successfully");
	}
	else
	{
		Logger::Get()->error("SwapChain creation failed: 0x{:X}", hr);

		// Try with original parameters as fallback
		if (hr != S_OK)
		{
			Logger::Get()->info("Retrying with original parameters");
			hr = s_TrueCreateSwapChain(self, pDevice, &originalDesc, ppSwapChain);
			if (SUCCEEDED(hr))
			{
				Logger::Get()->info("Fallback creation succeeded");
				s_pSwapChain = *ppSwapChain;
			}
		}
	}

	return hr;
}

HRESULT WINAPI WindowedMode::HookedD3D10CreateDeviceAndSwapChain(
	IDXGIAdapter* pAdapter, D3D10_DRIVER_TYPE DriverType, HMODULE Software,
	UINT Flags, UINT SDKVersion, DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
	IDXGISwapChain** ppSwapChain, ID3D10Device** ppDevice)
{
	Logger::Get()->info("HookedD3D10CreateDeviceAndSwapChain called");

	if (pSwapChainDesc)
	{
		AdjustSwapChainDesc(pSwapChainDesc);
		if (pSwapChainDesc->OutputWindow)
		{
			SetupWindow(pSwapChainDesc->OutputWindow);
		}
	}

	HRESULT hr = s_TrueD3D10CreateDeviceAndSwapChain(pAdapter, DriverType, Software,
		Flags, SDKVersion, pSwapChainDesc, ppSwapChain, ppDevice);

	if (SUCCEEDED(hr))
	{
		if (ppDevice && *ppDevice)
		{
			s_pDevice10 = *ppDevice;
			Logger::Get()->info("D3D10 device created via CreateDeviceAndSwapChain");
		}

		if (ppSwapChain && *ppSwapChain)
		{
			s_pSwapChain = *ppSwapChain;

			// Hook Present on the swap chain
			void** vtbl = *reinterpret_cast<void***>(s_pSwapChain);
			if (!s_TruePresent10)
			{
				MH_CreateHook(vtbl[8], &HookedPresent10, (void**)&s_TruePresent10);
				MH_EnableHook(vtbl[8]);
				Logger::Get()->info("Hooked IDXGISwapChain::Present via CreateDeviceAndSwapChain");
			}
		}
	}

	return hr;
}

HRESULT STDMETHODCALLTYPE WindowedMode::HookedPresent10(IDXGISwapChain* self, UINT SyncInterval, UINT Flags)
{
	// Ensure window setup is maintained
	if (s_gameWindow && (g_config.windowMode == 1 || g_config.windowMode == 2))
	{
		// Verify window style is still correct
		LONG currentStyle = GetWindowLong(s_gameWindow, GWL_STYLE);

		if (g_config.windowMode == 1) // Borderless
		{
			if (currentStyle & (WS_CAPTION | WS_THICKFRAME))
			{
				Logger::Get()->debug("Reapplying borderless style");
				LONG style = currentStyle;
				style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZE | WS_MAXIMIZE | WS_SYSMENU);
				style |= WS_POPUP;
				SetWindowLong(s_gameWindow, GWL_STYLE, style);
				SetWindowPos(s_gameWindow, HWND_TOP, 0, 0,
					GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
					SWP_FRAMECHANGED);
			}
		}
		else if (g_config.windowMode == 2) // Windowed
		{
			if (!(currentStyle & WS_OVERLAPPEDWINDOW))
			{
				Logger::Get()->debug("Reapplying windowed style");
				LONG style = currentStyle;
				style &= ~WS_POPUP;
				style |= WS_OVERLAPPEDWINDOW;
				SetWindowLong(s_gameWindow, GWL_STYLE, style);

				// Recalculate window size
				int width = g_config.windowWidth > 0 ? g_config.windowWidth : 1280;
				int height = g_config.windowHeight > 0 ? g_config.windowHeight : 720;

				RECT rect = { 0, 0, width, height };
				AdjustWindowRect(&rect, style, FALSE);

				SetWindowPos(s_gameWindow, HWND_TOP,
					g_config.windowPosX >= 0 ? g_config.windowPosX : (GetSystemMetrics(SM_CXSCREEN) - (rect.right - rect.left)) / 2,
					g_config.windowPosY >= 0 ? g_config.windowPosY : (GetSystemMetrics(SM_CYSCREEN) - (rect.bottom - rect.top)) / 2,
					rect.right - rect.left, rect.bottom - rect.top,
					SWP_FRAMECHANGED);
			}
		}
	}

	return s_TruePresent10(self, SyncInterval, Flags);
}

// DXGI Factory hook helper
HRESULT WINAPI WindowedMode::HookedCreateDXGIFactory(REFIID riid, void** ppFactory)
{
	HRESULT hr = s_TrueCreateDXGIFactory(riid, ppFactory);

	if (SUCCEEDED(hr) && ppFactory && *ppFactory)
	{
		IDXGIFactory* factory = (IDXGIFactory*)(*ppFactory);
		void** vtbl = *reinterpret_cast<void***>(factory);

		MH_CreateHook(vtbl[10], &WindowedMode::HookedCreateSwapChain,
			(void**)&WindowedMode::s_TrueCreateSwapChain);
		MH_EnableHook(vtbl[10]);

		Logger::Get()->info("Hooked IDXGIFactory::CreateSwapChain");
	}

	return hr;
}

//=============================================================================
// Focus Handling Hooks
//=============================================================================

BOOL WINAPI WindowedMode::HookedGetMessage(LPMSG lpMsg, HWND hWnd, UINT min, UINT max)
{
	BOOL result = s_TrueGetMessage(lpMsg, hWnd, min, max);
	if (result && lpMsg)
		FilterMessage(lpMsg);
	return result;
}

BOOL WINAPI WindowedMode::HookedPeekMessage(LPMSG lpMsg, HWND hWnd, UINT min, UINT max, UINT remove)
{
	BOOL result = s_TruePeekMessage(lpMsg, hWnd, min, max, remove);
	if (result && lpMsg && (remove & PM_REMOVE))
		FilterMessage(lpMsg);
	return result;
}

HWND WINAPI WindowedMode::HookedGetForegroundWindow()
{
	HWND out = (s_blockFocusLoss && s_gameWindow) ? s_gameWindow : s_TrueGetForegroundWindow();
	Logger::Get()->debug("HookedGetForegroundWindow → 0x{:X}", (uintptr_t)out);
	return out;
}

HWND WINAPI WindowedMode::HookedGetFocus()
{
	HWND out = (s_blockFocusLoss && s_gameWindow) ? s_gameWindow : s_TrueGetFocus();
	Logger::Get()->debug("HookedGetFocus → 0x{:X}", (uintptr_t)out);
	return out;
}

SHORT WINAPI WindowedMode::HookedGetAsyncKeyState(int vKey)
{
	return s_TrueGetAsyncKeyState(vKey);
}