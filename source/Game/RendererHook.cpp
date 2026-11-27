#include "Game/RendererHook.h"

#include "Logging/Logging.h"

#include <spdlog/spdlog.h>
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <new>
#include <safetyhook.hpp>
#include <utility>

namespace
{
    using AltairFix::Game::Renderer;

    /// Inline hook that confirms which renderer API called device creation.
    ///
    /// Allocated for the process lifetime. Removing the hook during DLL detach
    /// would run safetyhook's thread suspension while the loader lock is held.
    safetyhook::InlineHook* g_hook = nullptr;

    /// Ensures the confirmation is logged only once.
    std::atomic_bool g_deviceCreationReported = false;

    /// @brief Logs the first renderer device creation.
    ///
    /// @param rendererName Renderer name used in the log record.
    void ReportDeviceCreation(const char* rendererName)
    {
        if (!g_deviceCreationReported.exchange(true))
        {
            spdlog::info("[renderer] {} device creation observed t=+{}ms", rendererName, AltairFix::Logging::ElapsedMilliseconds());
        }
    }

    /// @brief Replacement for @c Direct3DCreate9.
    ///
    /// @param sdkVersion Direct3D SDK version supplied by the game.
    /// @return The interface returned by the original function.
    void* WINAPI HookDirect3DCreate9(std::uint32_t sdkVersion)
    {
        ReportDeviceCreation("Direct3D 9");
        return g_hook->stdcall<void*>(sdkVersion);
    }

    /// @brief Replacement for @c D3D10CreateDevice.
    ///
    /// The renderer types are kept as plain integers because the hook only
    /// forwards arguments and never inspects Direct3D structures.
    ///
    /// @param adapter DXGI adapter.
    /// @param driverType Direct3D 10 driver type.
    /// @param software Software rasterizer module.
    /// @param flags Device creation flags.
    /// @param sdkVersion Direct3D SDK version.
    /// @param device Receives the created device.
    /// @return The HRESULT returned by the original function.
    long WINAPI HookD3D10CreateDevice(void* adapter, int driverType, HMODULE software, std::uint32_t flags, std::uint32_t sdkVersion, void** device)
    {
        ReportDeviceCreation("Direct3D 10");
        return g_hook->stdcall<long>(adapter, driverType, software, flags, sdkVersion, device);
    }

    /// @brief Resolves the renderer module and device creation export.
    ///
    /// @param renderer Renderer selected at startup.
    /// @param moduleName Receives the wide renderer module name.
    /// @param moduleLabel Receives the narrow renderer module name for logs.
    /// @param functionName Receives the device creation export name.
    /// @param destination Receives the safetyhook replacement function.
    /// @return @c true when the renderer has a known hook target.
    [[nodiscard]] bool ResolveHookTarget(Renderer renderer, const wchar_t*& moduleName, const char*& moduleLabel, const char*& functionName, void*& destination)
    {
        switch (renderer)
        {
        case Renderer::Direct3D9:
            moduleName = L"d3d9.dll";
            moduleLabel = "d3d9.dll";
            functionName = "Direct3DCreate9";
            destination = reinterpret_cast<void*>(&HookDirect3DCreate9);
            return true;
        case Renderer::Direct3D10:
            moduleName = L"d3d10.dll";
            moduleLabel = "d3d10.dll";
            functionName = "D3D10CreateDevice";
            destination = reinterpret_cast<void*>(&HookD3D10CreateDevice);
            return true;
        case Renderer::Unknown:
            break;
        }

        return false;
    }
} // namespace

namespace AltairFix::Game
{
    void InstallRendererConfirmationHook(Renderer renderer)
    {
        if (g_hook != nullptr)
        {
            return;
        }

        const wchar_t* moduleName = nullptr;
        const char* moduleLabel = nullptr;
        const char* functionName = nullptr;
        void* destination = nullptr;
        if (!ResolveHookTarget(renderer, moduleName, moduleLabel, functionName, destination))
        {
            spdlog::warn("[renderer] confirmation hook skipped reason=unknown-renderer");
            return;
        }

        HMODULE module = GetModuleHandleW(moduleName);
        if (module == nullptr)
        {
            spdlog::warn("[renderer] confirmation hook skipped name={} module={} reason=module-not-loaded", GetRendererName(renderer), moduleLabel);
            return;
        }

        FARPROC target = GetProcAddress(module, functionName);
        if (target == nullptr)
        {
            spdlog::warn("[renderer] confirmation hook skipped name={} module={} export={} reason=export-not-found",
                         GetRendererName(renderer),
                         moduleLabel,
                         functionName);
            return;
        }

        spdlog::info("[renderer] confirmation hook target name={} module={} export={} address=0x{:X}",
                     GetRendererName(renderer),
                     moduleLabel,
                     functionName,
                     reinterpret_cast<std::uintptr_t>(target));

        // Create disabled so the callback cannot run before g_hook points at the
        // installed hook object.
        auto created = safetyhook::InlineHook::create(target, destination, safetyhook::InlineHook::StartDisabled);
        if (!created)
        {
            spdlog::warn("[renderer] confirmation hook install failed name={} target=0x{:X} safetyhook_error={}",
                         GetRendererName(renderer),
                         reinterpret_cast<std::uintptr_t>(target),
                         static_cast<int>(created.error().type));
            return;
        }

        auto* hook = new (std::nothrow) safetyhook::InlineHook(std::move(*created));
        if (hook == nullptr)
        {
            spdlog::warn("[renderer] confirmation hook install failed name={} target=0x{:X} reason=out-of-memory",
                         GetRendererName(renderer),
                         reinterpret_cast<std::uintptr_t>(target));
            return;
        }

        g_hook = hook;
        const auto enabled = g_hook->enable();
        if (!enabled)
        {
            spdlog::warn("[renderer] confirmation hook enable failed name={} target=0x{:X} safetyhook_error={}",
                         GetRendererName(renderer),
                         reinterpret_cast<std::uintptr_t>(target),
                         static_cast<int>(enabled.error().type));
            delete g_hook;
            g_hook = nullptr;
            return;
        }

        spdlog::info("[renderer] confirmation hook installed name={} module={} export={} address=0x{:X} hook=0x{:X} t=+{}ms",
                     GetRendererName(renderer),
                     moduleLabel,
                     functionName,
                     reinterpret_cast<std::uintptr_t>(target),
                     reinterpret_cast<std::uintptr_t>(hook),
                     AltairFix::Logging::ElapsedMilliseconds());
    }
} // namespace AltairFix::Game