/// @file dllmain.cpp
/// @brief ASI entry point for AltairFix.
///
/// Ultimate ASI Loader loads this DLL into the game process. DllMain keeps the
/// loader-lock window short:
///
/// 1. Load AltairFix.toml and detect the DX9 or DX10 executable.
/// 2. Initialize file logging next to the ASI (see Logging/Logging.h).
/// 3. Disable per-thread loader notifications, which the mod does not need.
/// 4. Hand the welcome dialog and safetyhook installer to worker threads, so a
///    modal MessageBox and thread suspension never run under the loader lock.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "Config/Config.h"
#include "Game/Executable.h"
#include "Game/RendererHook.h"
#include "Logging/Logging.h"
#include "Paths/Paths.h"

#include <spdlog/spdlog.h>
#include <windows.h>

namespace
{
    /// Renderer detected during DLL_PROCESS_ATTACH. The hook worker starts after
    /// DllMain releases the loader lock and reads this value.
    AltairFix::Game::Renderer g_detectedRenderer = AltairFix::Game::Renderer::Unknown;

    /// @brief Worker-thread entry point that shows the placeholder welcome
    ///        dialog.
    ///
    /// The dialog is modal, so it is deliberately kept out of DllMain. The log
    /// record before MessageBoxW() proves that injection succeeded even if the
    /// user leaves the dialog open or closes the game before dismissing it.
    ///
    /// @param unused Unused thread parameter required by the
    ///               @c LPTHREAD_START_ROUTINE signature.
    /// @return Always 0.
    DWORD WINAPI ShowInjectedMessage(LPVOID /*unused*/)
    {
        spdlog::info("Displaying the placeholder welcome message.");
        MessageBoxW(nullptr,
                    L"AltairFix injected successfully!\n\nThis is the placeholder mod entry point.",
                    L"AltairFix",
                    MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND | MB_TOPMOST);
        return 0;
    }

    /// @brief Worker-thread entry point that installs the renderer hook.
    ///
    /// safetyhook suspends other threads while patching code. Running it from
    /// DllMain would do that while the loader lock is held, so the hook is
    /// installed from a dedicated thread after DllMain returns.
    ///
    /// @param unused Unused thread parameter required by the
    ///               @c LPTHREAD_START_ROUTINE signature.
    /// @return Always 0.
    DWORD WINAPI InstallRendererHook(LPVOID /*unused*/)
    {
        AltairFix::Game::InstallRendererConfirmationHook(g_detectedRenderer);
        return 0;
    }
} // namespace

/// @brief DLL entry point invoked by Ultimate ASI Loader.
///
/// On @c DLL_PROCESS_ATTACH the function loads @c AltairFix.toml, detects the
/// renderer build, prepares logging, and starts the startup worker threads.
/// Other notifications are ignored except for @c DLL_PROCESS_DETACH, which
/// flushes and closes the log.
///
/// @param module Module handle of this ASI. It determines where the log file is
///               written.
/// @param reason Loader notification code.
/// @param reserved Reserved by Windows; unused.
/// @return Always @c TRUE so the loader keeps the ASI mapped.
extern "C" BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID /*reserved*/)
{
    if (reason == DLL_PROCESS_DETACH)
    {
        // Flush pending records before the process releases the ASI.
        AltairFix::Logging::Shutdown();
        return TRUE;
    }

    if (reason != DLL_PROCESS_ATTACH)
    {
        return TRUE;
    }

    DisableThreadLibraryCalls(module);

    // Read the configuration first: its [debug] table decides whether and at
    // what verbosity file logging is created.
    const AltairFix::Config::LoadResult config = AltairFix::Config::Load(module);
    const AltairFix::Logging::Options loggingOptions{.enabled = config.settings.debug.enableLogging, .verbosity = config.settings.debug.logLevel};
    const AltairFix::Game::Renderer renderer = AltairFix::Game::DetectRenderer(GetModuleHandleW(nullptr));

    // Open the log before any other work so early failures and the exact ASI
    // location are captured for support reports.
    const bool loggingReady = AltairFix::Logging::Initialize(module, loggingOptions);
    if (loggingReady)
    {
        spdlog::info("AltairFix {} attached to the game process.", config.settings.metadata.version);
        if (!config.path.empty())
        {
            spdlog::info("Configuration: {}{}.", AltairFix::Paths::ToUtf8(config.path.wstring()), config.createdDefaultFile ? " (defaults created)" : "");
        }
        if (!config.warning.empty())
        {
            spdlog::warn("{}", config.warning);
        }
        spdlog::info("Renderer: {} (detected from the executable import table).", AltairFix::Game::GetRendererName(renderer));
        if (renderer == AltairFix::Game::Renderer::Unknown)
        {
            spdlog::warn("Renderer could not be identified; version-specific features will stay disabled.");
        }
        spdlog::debug("Debug logging is enabled.");
    }

    if (renderer != AltairFix::Game::Renderer::Unknown)
    {
        // SafetyHook is installed after DllMain releases the loader lock.
        g_detectedRenderer = renderer;
        HANDLE hookThread = CreateThread(nullptr, 0, InstallRendererHook, nullptr, 0, nullptr);
        if (hookThread != nullptr)
        {
            CloseHandle(hookThread);
        }
        else if (loggingReady)
        {
            spdlog::error("Failed to create the renderer-hook thread (error {}).", GetLastError());
        }
    }

    HANDLE messageThread = CreateThread(nullptr, 0, ShowInjectedMessage, nullptr, 0, nullptr);
    if (messageThread != nullptr)
    {
        CloseHandle(messageThread);
    }
    else if (loggingReady)
    {
        spdlog::error("Failed to create the welcome-message thread (error {}).", GetLastError());
    }

    return TRUE;
}