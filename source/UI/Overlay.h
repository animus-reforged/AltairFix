#ifndef ALTAIRFIX_UI_OVERLAY_H
#define ALTAIRFIX_UI_OVERLAY_H

#include "Game/Executable.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <d3d9.h>
#include <windows.h>

namespace AltairFix::UI::Overlay
{
    /// @brief Creates the ImGui context and initializes both backends.
    ///
    /// @param device Game's Direct3D 9 device.
    /// @param hwnd Game focus window.
    /// @param renderer Renderer detected from the executable.
    void Initialize(IDirect3DDevice9* device, HWND hwnd, Game::Renderer renderer);

    /// @brief Draws one overlay frame while the debug window or the welcome
    ///        notification is visible.
    void Render();

    /// @brief Records that the game has presented its first frame.
    void NotifyGamePresented();

    /// @brief Toggles the overlay window.
    void Toggle();

    /// @brief Returns true once initialization has been attempted.
    [[nodiscard]] bool IsInitialized();

    /// @brief Returns whether the overlay window is visible.
    [[nodiscard]] bool IsVisible();
} // namespace AltairFix::UI::Overlay

#endif // ALTAIRFIX_UI_OVERLAY_H
