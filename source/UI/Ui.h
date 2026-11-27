#ifndef ALTAIRFIX_UI_UI_H
#define ALTAIRFIX_UI_UI_H

#include "Game/Executable.h"

namespace AltairFix::UI
{
    /// @brief Draws the AltairFix debug/settings window.
    ///
    /// @param renderer Detected renderer.
    /// @param visible Window visibility flag; the close button clears it.
    void Draw(Game::Renderer renderer, bool* visible);

    /// @brief Draws the non-interactive welcome notification.
    /// @param renderer Detected renderer.
    /// @param alpha Fade alpha in [0, 1].
    void DrawWelcomeNotification(Game::Renderer renderer, float alpha);
} // namespace AltairFix::UI

#endif // ALTAIRFIX_UI_UI_H
