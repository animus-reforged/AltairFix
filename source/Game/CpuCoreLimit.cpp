#include "Game/CpuCoreLimit.h"

#include <spdlog/spdlog.h>

#include <atomic>
#include <cstdint>
#include <string_view>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

namespace
{
    /// Preferred image base of the DX9 executable.
    constexpr std::uintptr_t kImageBase = 0x00400000;

    /// IAT slot for KERNEL32.GetSystemInfo in the retail DX9 build.
    constexpr std::uintptr_t kGetSystemInfoIatRva = 0x01647C5C;

    /// IAT slot for KERNEL32.GetProcessAffinityMask in the retail DX9 build.
    constexpr std::uintptr_t kGetProcessAffinityMaskIatRva = 0x01647BC8;

    /// Largest processor count the game accepts.
    constexpr DWORD kMaximumReportedProcessors = 31;

    /// Affinity mask with the low 31 processor bits set.
    constexpr DWORD_PTR kMaximumAffinityMask = 0x7FFFFFFFU;

    using GetSystemInfoFn = void(WINAPI*)(LPSYSTEM_INFO);
    using GetProcessAffinityMaskFn = BOOL(WINAPI*)(HANDLE, PDWORD_PTR, PDWORD_PTR);

    /// Original kernel32 functions saved before the IAT patches.
    GetSystemInfoFn g_originalGetSystemInfo = nullptr;
    GetProcessAffinityMaskFn g_originalGetProcessAffinityMask = nullptr;

    /// Whether the hook currently clamps the processor count.
    std::atomic_bool g_enabled{true};

    /// Whether the IAT patch was applied successfully.
    bool g_installed = false;

    /// Human-readable installation outcome for diagnostics.
    const char* g_status = "not-attempted";

    /// @brief Replaces one imported function pointer in the game's IAT.
    ///
    /// @return "installed" on success, or a short failure reason.
    template <typename Fn> [[nodiscard]] const char* PatchImport(std::uint8_t* moduleBase, std::uintptr_t rva, FARPROC real, Fn replacement, Fn& original)
    {
        auto* slot = reinterpret_cast<Fn*>(moduleBase + rva);
        Fn current = *slot;
        if (reinterpret_cast<std::uintptr_t>(current) != reinterpret_cast<std::uintptr_t>(real))
        {
            // Another hook may own the slot; do not fight it.
            return "iat-mismatch";
        }

        DWORD oldProtection = 0;
        if (VirtualProtect(static_cast<void*>(slot), sizeof(*slot), PAGE_READWRITE, &oldProtection) == 0)
        {
            return "virtual-protect-failed";
        }

        original = current;
        *slot = replacement;

        DWORD ignored = 0;
        VirtualProtect(static_cast<void*>(slot), sizeof(*slot), oldProtection, &ignored);
        return "installed";
    }

    /// @brief Replacement that caps the processor count reported to the game.
    ///
    /// @param info Receives the system information.
    void WINAPI HookGetSystemInfo(LPSYSTEM_INFO info)
    {
        if (g_originalGetSystemInfo == nullptr)
        {
            if (info != nullptr)
            {
                ZeroMemory(info, sizeof(SYSTEM_INFO));
            }
            return;
        }

        g_originalGetSystemInfo(info);

        if (info != nullptr && g_enabled.load(std::memory_order_relaxed) && info->dwNumberOfProcessors > kMaximumReportedProcessors)
        {
            info->dwNumberOfProcessors = kMaximumReportedProcessors;
            // Keep the affinity mask consistent with the reported core count.
            info->dwActiveProcessorMask &= kMaximumAffinityMask;
        }
    }

    /// @brief Replacement that caps the affinity masks reported to the game.
    ///
    /// The game's CPU detection loops over every set bit in the process mask
    /// using `1 << (index & 31)`. With 32 set bits that expression wraps and the
    /// loop never ends, so both returned masks are capped at 31 bits.
    ///
    /// @param hProcess Process whose affinity is queried.
    /// @param processMask Receives the process affinity mask.
    /// @param systemMask Receives the system affinity mask.
    /// @return The original function's result.
    BOOL WINAPI HookGetProcessAffinityMask(HANDLE hProcess, PDWORD_PTR processMask, PDWORD_PTR systemMask)
    {
        if (g_originalGetProcessAffinityMask == nullptr)
        {
            return FALSE;
        }

        const BOOL result = g_originalGetProcessAffinityMask(hProcess, processMask, systemMask);
        if (result != 0 && g_enabled.load(std::memory_order_relaxed))
        {
            if (processMask != nullptr)
            {
                *processMask &= kMaximumAffinityMask;
            }
            if (systemMask != nullptr)
            {
                *systemMask &= kMaximumAffinityMask;
            }
        }
        return result;
    }
} // namespace

namespace AltairFix::Game
{
    void InstallCpuCoreLimit(bool enabled)
    {
        g_enabled.store(enabled, std::memory_order_relaxed);

        HMODULE module = GetModuleHandleW(nullptr);
        if (module == nullptr)
        {
            g_status = "main-module-unavailable";
            return;
        }

        auto* moduleBase = reinterpret_cast<std::uint8_t*>(module);
        HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        if (kernel32 == nullptr)
        {
            g_status = "kernel32-unavailable";
            return;
        }

        FARPROC realSystemInfo = GetProcAddress(kernel32, "GetSystemInfo");
        if (realSystemInfo == nullptr)
        {
            g_status = "getsysteminfo-unavailable";
            return;
        }

        const char* status = PatchImport(moduleBase, kGetSystemInfoIatRva, realSystemInfo, &HookGetSystemInfo, g_originalGetSystemInfo);
        if (std::string_view(status) != "installed")
        {
            g_status = status;
            return;
        }

        FARPROC realAffinity = GetProcAddress(kernel32, "GetProcessAffinityMask");
        if (realAffinity == nullptr)
        {
            g_status = "getprocessaffinitymask-unavailable";
            return;
        }

        status = PatchImport(moduleBase, kGetProcessAffinityMaskIatRva, realAffinity, &HookGetProcessAffinityMask, g_originalGetProcessAffinityMask);
        g_status = status;
        g_installed = std::string_view(status) == "installed";
    }

    bool IsCpuCoreLimitEnabled()
    {
        return g_enabled.load(std::memory_order_relaxed);
    }

    void SetCpuCoreLimitEnabled(bool enabled)
    {
        g_enabled.store(enabled, std::memory_order_relaxed);
    }

    DWORD GetActualProcessorCount()
    {
        SYSTEM_INFO info{};
        if (g_originalGetSystemInfo != nullptr)
        {
            g_originalGetSystemInfo(&info);
        }
        else
        {
            ::GetSystemInfo(&info);
        }
        return info.dwNumberOfProcessors;
    }

    DWORD GetEffectiveProcessorCount()
    {
        DWORD count = GetActualProcessorCount();
        if (g_enabled.load(std::memory_order_relaxed) && count > kMaximumReportedProcessors)
        {
            count = kMaximumReportedProcessors;
        }
        return count;
    }

    void LogCpuCoreLimitStatus()
    {
        spdlog::info("[cpu] core limit installed={} enabled={} status={} actual={} reported={} max={}",
                     g_installed,
                     g_enabled.load(std::memory_order_relaxed),
                     g_status,
                     GetActualProcessorCount(),
                     GetEffectiveProcessorCount(),
                     kMaximumReportedProcessors);
    }
} // namespace AltairFix::Game
