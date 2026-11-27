#ifndef ALTAIRFIX_UI_INPUT_H
#define ALTAIRFIX_UI_INPUT_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

namespace AltairFix::UI::Input
{
    /// @brief Subclasses the game window to handle Insert and ImGui input.
    ///
    /// @param hwnd Game focus window from IDirect3DDevice9::GetCreationParameters.
    void Install(HWND hwnd);
} // namespace AltairFix::UI::Input

#endif // ALTAIRFIX_UI_INPUT_H
