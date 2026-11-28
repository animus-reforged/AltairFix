#include "Game/RendererHook.h"

#include "Logging/Logging.h"

#include <spdlog/spdlog.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <new>
#include <safetyhook.hpp>
#include <utility>

namespace
{
    using AltairFix::Game::Renderer;

    /// Preferred image base of the DX9 executable.
    constexpr std::uintptr_t kImageBase = 0x00400000;

    /// First byte of the signature checked before installing the cleanup guard.
    constexpr std::uintptr_t kDisplayCleanupSignatureAddress = 0x00A1ED02;

    /// Store instruction inside FUN_00a1ecd0 that receives the bad pointer.
    constexpr std::uintptr_t kDisplayCleanupStoreAddress = 0x00A1ED0B;

    /// First byte of the signature for the cleanup-loop count check.
    constexpr std::uintptr_t kDisplayCleanupCountSignatureAddress = 0x00A50D3D;

    /// Branch that skips the cleanup loop when its count is implausible.
    constexpr std::uintptr_t kDisplayCleanupCountCheckAddress = 0x00A50D40;

    /// Bytes from 0xA50D3D through 0xA50D58 in the retail DX9 build:
    /// cmp [ebx+0x18],edi; jle ...; push esi; lea esi,[ebx+4]; ...
    constexpr std::array<std::uint8_t, 28> kDisplayCleanupCountSignature = {
        0x39, 0x7B, 0x18, 0x7E, 0x23, 0x56, 0x8D, 0x73, 0x04, 0x83, 0x3E, 0x00, 0x74, 0x0E,
        0x8B, 0x4E, 0xFC, 0xE8, 0xBD, 0x41, 0x05, 0x00, 0xC7, 0x06, 0x00, 0x00, 0x00, 0x00,
    };

    /// Largest cleanup count considered to belong to a valid container.
    constexpr int kMaximumDisplayCleanupEntries = 0x1000;

    /// x86 EFLAGS zero flag. Setting it forces the following @c jle.
    constexpr std::uintptr_t kEflagsZeroFlag = 0x40;

    /// Bytes from 0xA1ED02 through 0xA1ED16 in the retail DX9 build:
    /// mov edx,[esi+8]; add edx,0x670; mov [ecx+0x110],edx; pop esi; jmp ...
    constexpr std::array<std::uint8_t, 21> kDisplayCleanupSignature = {
        0x8B, 0x56, 0x08, 0x81, 0xC2, 0x70, 0x06, 0x00, 0x00, 0x89, 0x91, 0x10, 0x01, 0x00, 0x00, 0x5E, 0xE9, 0x19, 0x9E, 0xFC, 0xFF,
    };

    /// Inline hook that confirms which renderer API called device creation.
    ///
    /// Allocated for the process lifetime. Removing the hook during DLL detach
    /// would run safetyhook's thread suspension while the loader lock is held.
    safetyhook::InlineHook* g_hook = nullptr;

    /// MidHook that prevents the display cleanup from dereferencing 0x670.
    safetyhook::MidHook* g_displayCleanupGuard = nullptr;

    /// MidHook that skips the display cleanup when its count is implausible.
    safetyhook::MidHook* g_displayCleanupCountGuard = nullptr;

    /// Ensures the confirmation is logged only once.
    std::atomic_bool g_deviceCreationReported = false;

    /// Ensures the cleanup guard's first trigger is logged at warning level.
    std::atomic_bool g_displayCleanupGuardReported = false;

    /// Ensures the count guard's first trigger is logged at warning level.
    std::atomic_bool g_displayCleanupCountGuardReported = false;

    /// Whether the display cleanup guards currently alter game behaviour.
    std::atomic_bool g_rendererGuardEnabled = true;

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

    /// @brief Replaces the null-plus-offset store in @c FUN_00a1ecd0.
    ///
    /// At the hooked store, ECX is the display object, ESI is the renderer, and
    /// EDX is `renderer->display + 0x670`. When the renderer has no display
    /// pointer, EDX is the bare offset 0x670. The following cleanup treats that
    /// as an object pointer, so store null and clear the slot's active flag
    /// instead. The cleanup loop skips inactive slots.
    // NOLINTNEXTLINE(misc-const-correctness) - safetyhook passes a mutable Context.
    void OnDisplayCleanupStore(safetyhook::Context& ctx)
    {
        if (!g_rendererGuardEnabled.load(std::memory_order_relaxed))
        {
            return;
        }

        constexpr std::uintptr_t kMinimumUserPointer = 0x10000;
        if (ctx.edx >= kMinimumUserPointer)
        {
            return;
        }

        const std::uintptr_t badPointer = ctx.edx;
        ctx.edx = 0;

        if (ctx.ecx != 0)
        {
            auto* activeFlag = reinterpret_cast<std::uint32_t*>(ctx.ecx + 0x114); // NOLINT(performance-no-int-to-ptr)
            *activeFlag = 0;
        }

        if (!g_displayCleanupGuardReported.exchange(true, std::memory_order_acq_rel))
        {
            spdlog::warn("[renderer] display cleanup store guard prevented invalid slot renderer=0x{:X} display=0x{:X} bad_pointer=0x{:X} action=cleared "
                         "further_occurrences=suppressed",
                         static_cast<std::uintptr_t>(ctx.esi),
                         static_cast<std::uintptr_t>(ctx.ecx),
                         badPointer);
        }
    }

    /// @brief Skips the display cleanup when its container count is implausible.
    ///
    /// The cleanup loop uses `container+0x18` as an entry count. During the
    /// broken renderer reset path that field contains a pointer or code address,
    /// so the loop walks far past the real container and calls destructors on
    /// arbitrary values. If the count is larger than any real display container,
    /// set ZF so the following @c jle jumps to the function epilogue instead.
    // NOLINTNEXTLINE(misc-const-correctness) - safetyhook passes a mutable Context.
    void OnDisplayCleanupCount(safetyhook::Context& ctx)
    {
        if (!g_rendererGuardEnabled.load(std::memory_order_relaxed))
        {
            return;
        }

        if (ctx.ebx == 0)
        {
            return;
        }

        const int count = *reinterpret_cast<const int*>(ctx.ebx + 0x18); // NOLINT(performance-no-int-to-ptr)
        if (count <= kMaximumDisplayCleanupEntries)
        {
            return;
        }

        // ZF is set, so the jle at 0xA50D40 jumps to the epilogue at 0xA50D65.
        ctx.eflags |= kEflagsZeroFlag;

        if (!g_displayCleanupCountGuardReported.exchange(true, std::memory_order_acq_rel))
        {
            spdlog::warn("[renderer] display cleanup count guard skipped implausible container container=0x{:X} count={} max={} action=skip "
                         "further_occurrences=suppressed",
                         static_cast<std::uintptr_t>(ctx.ebx),
                         count,
                         kMaximumDisplayCleanupEntries);
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

    void InstallRendererDisplayCleanupGuard(Renderer renderer)
    {
        if (renderer != Renderer::Direct3D9)
        {
            spdlog::debug("[renderer] display cleanup guard skipped renderer={} reason=dx9-only", GetRendererName(renderer));
            return;
        }

        if (g_displayCleanupGuard != nullptr && g_displayCleanupCountGuard != nullptr)
        {
            return;
        }

        HMODULE module = GetModuleHandleW(nullptr);
        if (module == nullptr)
        {
            spdlog::warn("[renderer] display cleanup guard skipped reason=main-module-unavailable");
            return;
        }

        auto* moduleBase = reinterpret_cast<std::uint8_t*>(module);

        const auto install = [moduleBase](std::uintptr_t signatureAddress,
                                          std::uintptr_t targetAddress,
                                          const auto& signature,
                                          safetyhook::MidHookFn callback,
                                          safetyhook::MidHook** destination,
                                          const char* label)
        {
            const auto* signaturePtr = moduleBase + (signatureAddress - kImageBase);
            if (!std::equal(signature.begin(), signature.end(), signaturePtr))
            {
                spdlog::warn("[renderer] {} skipped reason=signature-mismatch signature=0x{:X} module_base=0x{:X}",
                             label,
                             reinterpret_cast<std::uintptr_t>(signaturePtr),
                             reinterpret_cast<std::uintptr_t>(moduleBase));
                return;
            }

            auto* target = moduleBase + (targetAddress - kImageBase);
            auto created = safetyhook::MidHook::create(static_cast<void*>(target), callback);
            if (!created)
            {
                spdlog::warn("[renderer] {} install failed target=0x{:X} safetyhook_error={}",
                             label,
                             reinterpret_cast<std::uintptr_t>(target),
                             static_cast<int>(created.error().type));
                return;
            }

            auto* hook = new (std::nothrow) safetyhook::MidHook(std::move(*created));
            if (hook == nullptr)
            {
                spdlog::warn("[renderer] {} install failed target=0x{:X} reason=out-of-memory", label, reinterpret_cast<std::uintptr_t>(target));
                return;
            }

            *destination = hook;
            spdlog::info("[renderer] {} installed target=0x{:X} signature=0x{:X} module_base=0x{:X} t=+{}ms",
                         label,
                         reinterpret_cast<std::uintptr_t>(target),
                         reinterpret_cast<std::uintptr_t>(signaturePtr),
                         reinterpret_cast<std::uintptr_t>(moduleBase),
                         AltairFix::Logging::ElapsedMilliseconds());
        };

        if (g_displayCleanupGuard == nullptr)
        {
            install(kDisplayCleanupSignatureAddress,
                    kDisplayCleanupStoreAddress,
                    kDisplayCleanupSignature,
                    &OnDisplayCleanupStore,
                    &g_displayCleanupGuard,
                    "display cleanup store guard");
        }

        if (g_displayCleanupCountGuard == nullptr)
        {
            install(kDisplayCleanupCountSignatureAddress,
                    kDisplayCleanupCountCheckAddress,
                    kDisplayCleanupCountSignature,
                    &OnDisplayCleanupCount,
                    &g_displayCleanupCountGuard,
                    "display cleanup count guard");
        }
    }

    bool IsRendererCrashGuardEnabled()
    {
        return g_rendererGuardEnabled.load(std::memory_order_relaxed);
    }

    void SetRendererCrashGuardEnabled(bool enabled)
    {
        g_rendererGuardEnabled.store(enabled, std::memory_order_relaxed);
    }
} // namespace AltairFix::Game
