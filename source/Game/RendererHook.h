/// @file RendererHook.h
/// @brief SafetyHook integration for renderer detection.
///
/// The executable import table identifies the DX9 or DX10 build before the game
/// creates a device. This module adds a safetyhook inline hook to the matching
/// device creation API so the first call confirms the runtime renderer and
/// provides a ready hook point for future renderer work.

#ifndef ALTAIRFIX_GAME_RENDERERHOOK_H
#define ALTAIRFIX_GAME_RENDERERHOOK_H

#include "Game/Executable.h"

namespace AltairFix::Game
{
    /// @brief Hooks the detected renderer's device creation function.
    ///
    /// Installs a safetyhook inline hook on @c Direct3DCreate9 for DX9 or
    /// @c D3D10CreateDevice for DX10. The hook logs the first device creation
    /// call and forwards to the original function unchanged.
    ///
    /// @param renderer Renderer detected from the executable import table.
    ///
    /// @note Call this after @c DLL_PROCESS_ATTACH has returned so safetyhook
    ///       does not suspend threads while the loader lock is held. DllMain
    ///       starts a worker thread for that reason.
    /// @note Installed hooks live for the process lifetime; the mod does not
    ///       support unloading a hooked ASI.
    void InstallRendererConfirmationHook(Renderer renderer);
} // namespace AltairFix::Game

#endif // ALTAIRFIX_GAME_RENDERERHOOK_H