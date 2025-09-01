#pragma once
#include <windows.h>
#include <d3d9.h>
#include <d3d10.h>
#include <dxgi.h>
#include "Engine.h"

class WindowedMode
{
public:
    static HWND GetGameWindow();
    static void Hook(EngineType engine);
    static void Unhook();

    // Engine-specific hooks
    static void HookD3D9();
    static void HookD3D10();
    static void HookFocusFunctions();

private:
    // Core state
    static HWND s_gameWindow;
    static bool s_blockFocusLoss;
    static WNDPROC s_OriginalWndProc;

    // Direct3D9 state
    static void** s_pD3D9VTable;
    static void** s_pDeviceVTable;
    static IDirect3DDevice9* s_pDevice;

    // Direct3D10/DXGI state
    static IDXGISwapChain* s_pSwapChain;
    static ID3D10Device* s_pDevice10;

    // D3D9 function pointers
    static HRESULT(WINAPI* s_TrueCreateDevice)(
        IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD,
        D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
    static HRESULT(WINAPI* s_TrueReset)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
    static HRESULT(WINAPI* s_TruePresent)(
        IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);

    // D3D10/DXGI function pointers
    static HRESULT(WINAPI* s_TrueD3D10CreateDevice)(
        IDXGIAdapter*, D3D10_DRIVER_TYPE, HMODULE, UINT, UINT, ID3D10Device**);
    static HRESULT(STDMETHODCALLTYPE* s_TrueCreateSwapChain)(
        IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
    static HRESULT(WINAPI* s_TrueD3D10CreateDeviceAndSwapChain)(
        IDXGIAdapter*, D3D10_DRIVER_TYPE, HMODULE, UINT, UINT,
        DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**, ID3D10Device**);
    static HRESULT(STDMETHODCALLTYPE* s_TruePresent10)(IDXGISwapChain*, UINT, UINT);
    static HRESULT WINAPI HookedCreateDXGIFactory(REFIID riid, void** ppFactory);

    // Focus handling function pointers
    static BOOL(WINAPI* s_TrueGetMessage)(LPMSG, HWND, UINT, UINT);
    static BOOL(WINAPI* s_TruePeekMessage)(LPMSG, HWND, UINT, UINT, UINT);
    static HWND(WINAPI* s_TrueGetForegroundWindow)();
    static HWND(WINAPI* s_TrueGetFocus)();
    static SHORT(WINAPI* s_TrueGetAsyncKeyState)(int);

    // D3D9 hooks
    static HRESULT WINAPI HookedCreateDevice(
        IDirect3D9* self, UINT adapter, D3DDEVTYPE devType,
        HWND hFocusWnd, DWORD behaviorFlags,
        D3DPRESENT_PARAMETERS* params, IDirect3DDevice9** ppDevice);
    static HRESULT WINAPI HookedReset(IDirect3DDevice9* self, D3DPRESENT_PARAMETERS* params);
    static HRESULT WINAPI HookedPresent(IDirect3DDevice9* self,
        const RECT* pSourceRect, const RECT* pDestRect,
        HWND hDestWindowOverride, const RGNDATA* pDirtyRegion);

    // D3D10/DXGI hooks
    static HRESULT WINAPI HookedD3D10CreateDevice(
        IDXGIAdapter* pAdapter, D3D10_DRIVER_TYPE DriverType,
        HMODULE Software, UINT Flags, UINT SDKVersion, ID3D10Device** ppDevice);
    static HRESULT STDMETHODCALLTYPE HookedCreateSwapChain(
        IDXGIFactory* self, IUnknown* pDevice,
        DXGI_SWAP_CHAIN_DESC* pDesc, IDXGISwapChain** ppSwapChain);
    static HRESULT WINAPI HookedD3D10CreateDeviceAndSwapChain(
        IDXGIAdapter* pAdapter, D3D10_DRIVER_TYPE DriverType, HMODULE Software,
        UINT Flags, UINT SDKVersion, DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
        IDXGISwapChain** ppSwapChain, ID3D10Device** ppDevice);
    static HRESULT STDMETHODCALLTYPE HookedPresent10(IDXGISwapChain* self, UINT SyncInterval, UINT Flags);

    // Focus handling hooks
    static BOOL WINAPI HookedGetMessage(LPMSG lpMsg, HWND hWnd, UINT wMin, UINT wMax);
    static BOOL WINAPI HookedPeekMessage(LPMSG lpMsg, HWND hWnd, UINT wMin, UINT wMax, UINT removeMsg);
    static HWND WINAPI HookedGetForegroundWindow();
    static HWND WINAPI HookedGetFocus();
    static SHORT WINAPI HookedGetAsyncKeyState(int vKey);

    // Window procedure
    static LRESULT CALLBACK WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Helper functions
    static void AdjustPresentParameters(D3DPRESENT_PARAMETERS* params);
    static void AdjustSwapChainDesc(DXGI_SWAP_CHAIN_DESC* desc);
    static void SetupWindow(HWND hWnd);
    static void FilterMessage(LPMSG lpMsg);
    static void ConfineCursorToWindow(HWND hWnd);
};