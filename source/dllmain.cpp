/// @file dllmain.cpp
/// @brief ASI entry point for AltairFix.
///
/// Ultimate ASI Loader loads this DLL into the game process. DllMain keeps the
/// loader-lock window short:
///
/// 1. Load AltairFix.toml and detect the DX9 or DX10 executable.
/// 2. Initialize file logging next to the ASI (see Logging/Logging.h).
/// 3. Disable per-thread loader notifications, which the mod does not need.
/// 4. Hand the safetyhook and overlay installers to a worker thread, so thread
///    suspension never runs under the loader lock.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "Config/Config.h"
#include "Game/Executable.h"
#include "Game/RendererHook.h"
#include "Logging/Logging.h"
#include "Paths/Paths.h"
#include "UI/Direct3D9Overlay.h"

#include <spdlog/spdlog.h>
#include <windows.h>

namespace
{
    /// Renderer detected during DLL_PROCESS_ATTACH. The hook worker starts after
    /// DllMain releases the loader lock and reads this value.
    AltairFix::Game::Renderer g_detectedRenderer = AltairFix::Game::Renderer::Unknown;

    /// @brief Worker-thread entry point that installs the renderer hook and
    ///        the in-game overlay.
    ///
    /// safetyhook suspends other threads while patching code. Running it from
    /// DllMain would do that while the loader lock is held, so the hooks are
    /// installed from a dedicated thread after DllMain returns.
    ///
    /// @param unused Unused thread parameter required by the
    ///               @c LPTHREAD_START_ROUTINE signature.
    /// @return Always 0.
    DWORD WINAPI InstallRendererHook(LPVOID /*unused*/)
    {
        AltairFix::Game::InstallRendererConfirmationHook(g_detectedRenderer);

        if (g_detectedRenderer == AltairFix::Game::Renderer::Direct3D9)
        {
            AltairFix::UI::InstallDirect3D9Overlay(g_detectedRenderer);
        }
        else if (g_detectedRenderer == AltairFix::Game::Renderer::Direct3D10)
        {
            spdlog::info("The in-game UI is not implemented for Direct3D 10 yet.");
        }

        spdlog::info("Startup message box removed; the in-game overlay is the new entry point.");
        return 0;
    }
} // namespace

/// @brief DLL entry point invoked by Ultimate ASI Loader.
///
/// On @c DLL_PROCESS_ATTACH the function loads @c AltairFix.toml, detects the
/// renderer build, prepares logging, and starts the renderer worker thread.
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
        spdlog::info("DirectX version: {}.", AltairFix::Game::GetDirectXVersionLabel(renderer));
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

    return TRUE;
}