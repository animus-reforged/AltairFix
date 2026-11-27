#include "UI/Direct3D9Overlay.h"

#include "UI/Overlay.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <new>
#include <safetyhook.hpp>
#include <utility>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <d3d9.h>
#include <windows.h>

namespace
{
    /// Preferred image base of the DX9 executable. The actual module base is
    /// used at runtime so the offset still works if the image was relocated.
    constexpr std::uintptr_t kImageBase = 0x00400000;

    /// Address of FUN_00A1EF10 in the executable.
    constexpr std::uintptr_t kEndDisplayAddress = 0x00A1EF10;

    /// Used to reject unknown game builds before safetyhook patches the process.
    constexpr std::array<std::uint8_t, 8> kEndDisplaySignature = {0x53, 0x8B, 0x5C, 0x24, 0x08, 0x55, 0x33, 0xED};

    /// Hook lives for the process lifetime.
    safetyhook::MidHook* g_hook = nullptr;

    /// Present gates. The overlay does not render until the game has presented
    /// at least one frame.
    safetyhook::MidHook* g_devicePresentHook = nullptr;
    safetyhook::MidHook* g_swapChainPresentHook = nullptr;
    bool g_presentGateAttempted = false;

    /// Set after the first window lookup failure so the per-frame hook stops
    /// retrying and stops logging.
    bool g_windowFailureLogged = false;

    /// @brief Marks the game as ready to host overlay frames.
    void OnGamePresent(safetyhook::Context& /*ctx*/)
    {
        AltairFix::UI::Overlay::NotifyGamePresented();
    }

    /// @brief Installs a MidHook on the device or swap-chain Present method.
    /// @param device Game's Direct3D 9 device.
    void InstallPresentGate(IDirect3DDevice9* device)
    {
        if (g_presentGateAttempted)
        {
            return;
        }
        g_presentGateAttempted = true;

        const auto install = [](void* target, safetyhook::MidHook** destination, const char* label)
        {
            if (target == nullptr)
            {
                return false;
            }

            auto created = safetyhook::MidHook::create(target, &OnGamePresent);
            if (!created)
            {
                spdlog::warn("Cannot install the {} present gate: safetyhook error {}.", label, static_cast<int>(created.error().type));
                return false;
            }

            auto* hook = new (std::nothrow) safetyhook::MidHook(std::move(*created));
            if (hook == nullptr)
            {
                spdlog::warn("Cannot install the {} present gate: out of memory.", label);
                return false;
            }

            *destination = hook;
            spdlog::info("{} present gate installed.", label);
            return true;
        };

        void* const* vtable = *reinterpret_cast<void***>(device);
        void* devicePresent = vtable != nullptr ? vtable[17] : nullptr; // IDirect3DDevice9::Present
        bool installed = install(devicePresent, &g_devicePresentHook, "Direct3D 9 device");

        IDirect3DSwapChain9* swapChain = nullptr;
        if (SUCCEEDED(device->GetSwapChain(0, &swapChain)) && swapChain != nullptr)
        {
            void* const* swapChainVtable = *reinterpret_cast<void***>(swapChain);
            void* swapChainPresent = swapChainVtable != nullptr ? swapChainVtable[3] : nullptr; // IDirect3DSwapChain9::Present
            swapChain->Release();

            if (swapChainPresent != nullptr && swapChainPresent != devicePresent)
            {
                installed = install(swapChainPresent, &g_swapChainPresentHook, "Direct3D 9 swap chain") || installed;
            }
        }

        if (!installed)
        {
            spdlog::warn("Cannot install a present gate; the overlay will render immediately.");
            AltairFix::UI::Overlay::NotifyGamePresented();
        }
    }

    /// @brief Draws the overlay at the end of the game's display render.
    ///
    /// FUN_00A1EF10 is called from FUN_00A1F5A0 after the game's render sets
    /// and Magma UI pass, but before the render targets are unbound. At entry,
    /// the stack argument is the DX9Device wrapper; the wrapper's first field
    /// is the live IDirect3DDevice9*.
    // NOLINTNEXTLINE(misc-const-correctness) - safetyhook passes a mutable Context.
    void OnEndDisplay(safetyhook::Context& ctx)
    {
        auto* deviceWrapper = *reinterpret_cast<void**>(ctx.esp + sizeof(std::uintptr_t)); // NOLINT(performance-no-int-to-ptr)
        if (deviceWrapper == nullptr)
        {
            return;
        }

        auto* device = *reinterpret_cast<IDirect3DDevice9**>(deviceWrapper);
        if (device == nullptr)
        {
            return;
        }

        if (!AltairFix::UI::Overlay::IsInitialized())
        {
            if (g_windowFailureLogged)
            {
                return;
            }

            D3DDEVICE_CREATION_PARAMETERS parameters{.DeviceType = D3DDEVTYPE_HAL};
            if (FAILED(device->GetCreationParameters(&parameters)) || parameters.hFocusWindow == nullptr)
            {
                g_windowFailureLogged = true;
                spdlog::warn("Cannot initialize the ImGui overlay: the game window is unavailable.");
                return;
            }

            AltairFix::UI::Overlay::Initialize(device, parameters.hFocusWindow, AltairFix::Game::Renderer::Direct3D9);
        }

        InstallPresentGate(device);
        AltairFix::UI::Overlay::Render();
    }
} // namespace

namespace AltairFix::UI
{
    void InstallDirect3D9Overlay(Game::Renderer renderer)
    {
        if (renderer != Game::Renderer::Direct3D9 || g_hook != nullptr)
        {
            return;
        }

        HMODULE module = GetModuleHandleW(nullptr);
        if (module == nullptr)
        {
            return;
        }

        auto* moduleBase = reinterpret_cast<std::uint8_t*>(module);
        auto* target = moduleBase + (kEndDisplayAddress - kImageBase);

        if (!std::equal(kEndDisplaySignature.begin(), kEndDisplaySignature.end(), target))
        {
            spdlog::warn("DX9 overlay disabled: the display hook signature does not match this executable.");
            return;
        }

        auto created = safetyhook::MidHook::create(static_cast<void*>(target), &OnEndDisplay);
        if (!created)
        {
            spdlog::warn("Cannot install the DX9 overlay hook: safetyhook error {}.", static_cast<int>(created.error().type));
            return;
        }

        g_hook = new (std::nothrow) safetyhook::MidHook(std::move(*created));
        if (g_hook == nullptr)
        {
            spdlog::warn("Cannot install the DX9 overlay hook: out of memory.");
            return;
        }

        spdlog::info("DX9 overlay hook installed at 0x{:X}.", reinterpret_cast<std::uintptr_t>(target));
    }
} // namespace AltairFix::UI
