/// @file Config.h
/// @brief TOML configuration loaded from beside the ASI.
///
/// AltairFix reads @c AltairFix.toml from the same directory as the loaded ASI.
/// When the file is missing it is created with the defaults below, so a fresh
/// installation has a documented configuration to edit. Missing tables or keys
/// keep their defaults, which allows partial files to remain valid.

#ifndef ALTAIRFIX_CONFIG_CONFIG_H
#define ALTAIRFIX_CONFIG_CONFIG_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <filesystem>
#include <string>

#ifndef ALTAIRFIX_VERSION
#define ALTAIRFIX_VERSION "0.0.0"
#endif

namespace AltairFix::Config
{
    /// Lowest accepted value for @c debug.log_level (trace).
    constexpr int MIN_LOG_LEVEL = 0;

    /// Highest accepted value for @c debug.log_level (off).
    constexpr int MAX_LOG_LEVEL = 6;

    /// Default logging verbosity (info), matching the legacy INI.
    constexpr int DEFAULT_LOG_LEVEL = 2;

    /// Default state for @c debug.enable_logging.
    constexpr bool DEFAULT_ENABLE_LOGGING = true;

    /// Default state for @c fixes.renderer_crash_guard.
    constexpr bool DEFAULT_RENDERER_CRASH_GUARD = true;

    /// Contents of the @c [metadata] table.
    struct Metadata
    {
        /// Mod version recorded in the configuration file. Informational.
        std::string version = ALTAIRFIX_VERSION;
    };

    /// Contents of the @c [debug] table.
    struct Debug
    {
        /// Whether file logging is enabled.
        bool enableLogging = DEFAULT_ENABLE_LOGGING;

        /// Verbosity from @c MIN_LOG_LEVEL (trace) to @c MAX_LOG_LEVEL (off).
        int logLevel = DEFAULT_LOG_LEVEL;
    };

    /// Contents of the @c [fixes] table.
    struct Fixes
    {
        /// Whether to guard the known DX9 renderer cleanup crash.
        bool rendererCrashGuard = DEFAULT_RENDERER_CRASH_GUARD;
    };

    /// All settings exposed by @c AltairFix.toml.
    struct Settings
    {
        Metadata metadata;
        Debug debug;
        Fixes fixes;
    };

    /// Outcome of loading @c AltairFix.toml.
    struct LoadResult
    {
        /// Parsed settings. Missing or invalid values keep their defaults.
        Settings settings;

        /// Resolved configuration path, or empty when the ASI path failed.
        std::filesystem::path path;

        /// Non-fatal problem to report after logging is initialized. Empty
        /// when the file was read cleanly.
        std::string warning;

        /// True when the file did not exist and the defaults were written.
        bool createdDefaultFile = false;

        /// True when missing known settings were added to an existing file.
        bool addedMissingDefaults = false;
    };

    /// @brief Loads settings from @c AltairFix.toml next to the ASI.
    ///
    /// Creates the file with documented defaults when it does not exist. A
    /// missing table or key keeps the corresponding default and is written back
    /// to an existing file, so partial files are upgraded while comments and
    /// unknown settings are preserved. A parse or I/O failure leaves settings at
    /// their defaults and fills @c LoadResult::warning.
    ///
    /// @param module ASI module used to locate the configuration file.
    /// @return Parsed settings, the resolved path, and any non-fatal warning.
    [[nodiscard]] LoadResult Load(HMODULE module);
} // namespace AltairFix::Config

#endif // ALTAIRFIX_CONFIG_CONFIG_H