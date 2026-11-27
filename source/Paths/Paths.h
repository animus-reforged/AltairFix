/// @file Paths.h
/// @brief Helpers for resolving files beside the loaded ASI.
///
/// The mod keeps its runtime files (configuration and log) in the same
/// directory as @c AltairFix.asi. These helpers centralize the Windows path
/// lookup so each feature reports the same location.

#ifndef ALTAIRFIX_PATHS_PATHS_H
#define ALTAIRFIX_PATHS_PATHS_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <filesystem>
#include <string>

namespace AltairFix::Paths
{
    /// @brief Returns the full path of the executable image for a module.
    ///
    /// The buffer grows until GetModuleFileNameW() reports a length that fits.
    /// A truncated call fills the buffer exactly without a terminator, so an
    /// exact-length result is retried with more room.
    ///
    /// @param module Module whose image path should be resolved.
    /// @return Absolute module path, or an empty path when Windows cannot
    ///         resolve the handle.
    [[nodiscard]] std::filesystem::path GetModulePath(HMODULE module);

    /// @brief Returns a path for a file in the directory that contains the
    ///        supplied module.
    ///
    /// @param module Module whose directory should be used.
    /// @param fileName File name, such as @c L"AltairFix.toml".
    /// @return Full path to the file, or an empty path when the module path
    ///         cannot be resolved.
    [[nodiscard]] std::filesystem::path GetFileNextToModule(HMODULE module, const wchar_t* fileName);

    /// @brief Converts wide text to UTF-8.
    ///
    /// spdlog and toml++ consume narrow strings, including paths. Conversion is
    /// explicit so non-ASCII installation paths survive the round trip.
    ///
    /// @param text Wide text to convert.
    /// @return UTF-8 representation, or an empty string when conversion fails.
    [[nodiscard]] std::string ToUtf8(const std::wstring& text);
} // namespace AltairFix::Paths

#endif // ALTAIRFIX_PATHS_PATHS_H