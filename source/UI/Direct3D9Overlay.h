#ifndef ALTAIRFIX_UI_DIRECT3D9OVERLAY_H
#define ALTAIRFIX_UI_DIRECT3D9OVERLAY_H

#include "Game/Executable.h"

namespace AltairFix::UI
{
    /// @brief Installs the DX9 display-teardown MidHook that draws ImGui.
    ///
    /// The hook is only installed when the executable matches the known DX9
    /// build signature. It reads the game's Direct3D 9 device from the hooked
    /// function's argument, so the overlay renders through the game's own
    /// device and window.
    ///
    /// @param renderer Renderer detected from the executable.
    void InstallDirect3D9Overlay(Game::Renderer renderer);
} // namespace AltairFix::UI

#endif // ALTAIRFIX_UI_DIRECT3D9OVERLAY_H
