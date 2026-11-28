/// @file CpuCoreLimit.h
/// @brief Caps the CPU core count reported to the game.
///
/// The retail build does not start on systems reporting 32 or more logical
/// processors. AltairFix patches the game module's GetSystemInfo import so it
/// observes at most 31 cores. The IAT patch is installed from DllMain, before
/// the game performs its startup check, and can be toggled at runtime from the
/// overlay.
///
/// The patch affects only the game module's import address table. Other modules
/// and the mod itself still see the real processor count.

#ifndef ALTAIRFIX_GAME_CPUCORELIMIT_H
#define ALTAIRFIX_GAME_CPUCORELIMIT_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

namespace AltairFix::Game
{
    /// @brief Installs the GetSystemInfo import hook for the game module.
    ///
    /// @param enabled Whether to clamp the reported count when installed.
    /// @note Call from DLL_PROCESS_ATTACH, before the game runs. The hook does
    ///       not use safetyhook; it only rewrites one import address table slot.
    void InstallCpuCoreLimit(bool enabled);

    /// @brief Returns whether clamping is currently active.
    [[nodiscard]] bool IsCpuCoreLimitEnabled();

    /// @brief Enables or disables clamping for subsequent GetSystemInfo calls.
    void SetCpuCoreLimitEnabled(bool enabled);

    /// @brief Returns the processor count reported by the real GetSystemInfo.
    [[nodiscard]] DWORD GetActualProcessorCount();

    /// @brief Returns the count the game will observe with the current setting.
    [[nodiscard]] DWORD GetEffectiveProcessorCount();

    /// @brief Logs the hook status after file logging has been initialized.
    void LogCpuCoreLimitStatus();
} // namespace AltairFix::Game

#endif // ALTAIRFIX_GAME_CPUCORELIMIT_H
