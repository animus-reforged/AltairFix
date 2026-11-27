/// @file Executable.h
/// @brief Identifies which renderer build of the game is running.
///
/// Assassin's Creed shipped with separate DX9 and DX10 executables. The two
/// builds have different code layouts, so features must know which one they are
/// running in before applying offsets or hooks.

#ifndef ALTAIRFIX_GAME_EXECUTABLE_H
#define ALTAIRFIX_GAME_EXECUTABLE_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <cstdint>

namespace AltairFix::Game
{
    /// Renderer builds that AltairFix supports.
    enum class Renderer : std::uint8_t
    {
        /// The executable could not be identified.
        Unknown,

        /// AssassinsCreed_Dx9.exe.
        Direct3D9,

        /// AssassinsCreed_Dx10.exe.
        Direct3D10,
    };

    /// @brief Detects the renderer build from a module's PE import table.
    ///
    /// The DX9 executable imports @c d3d9.dll. The DX10 executable imports
    /// @c d3d10.dll and @c dxgi.dll. Reading the import directory avoids relying
    /// on the executable file name and works even before the renderer DLL has
    /// finished loading.
    ///
    /// @param module Main executable module, normally
    ///               @c GetModuleHandleW(nullptr).
    /// @return Detected renderer, or @c Renderer::Unknown when the module is not
    ///         a readable 32-bit PE image or the import table cannot be parsed.
    [[nodiscard]] Renderer DetectRenderer(HMODULE module);

    /// @brief Returns a display name for log records.
    ///
    /// @param renderer Renderer value to describe.
    /// @return Static string such as @c "Direct3D 9".
    [[nodiscard]] const char* GetRendererName(Renderer renderer);

    /// @brief Returns the user-facing DirectX version label.
    ///
    /// @param renderer Renderer value to describe.
    /// @return Static string such as @c "DirectX 9" or @c "Unknown".
    [[nodiscard]] const char* GetDirectXVersionLabel(Renderer renderer);
} // namespace AltairFix::Game

#endif // ALTAIRFIX_GAME_EXECUTABLE_H