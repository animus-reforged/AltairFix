/// @file Logging.h
/// @brief Process-wide diagnostic logging for AltairFix.
///
/// The logging module owns the spdlog logger used by every part of the mod. It
/// is deliberately separated from the ASI entry point: feature code can write
/// diagnostics without knowing where the log file lives or how the sink is
/// constructed.
///
/// The log is written to @c AltairFix.log in the same directory as the loaded
/// @c AltairFix.asi file and is recreated on every launch. Support reports
/// therefore always describe a single game session instead of accumulating
/// stale lines from earlier runs.

#ifndef ALTAIRFIX_LOGGING_LOGGING_H
#define ALTAIRFIX_LOGGING_LOGGING_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

namespace AltairFix::Logging
{
    /// Runtime logging options supplied by the configuration file.
    struct Options
    {
        /// Whether file logging is enabled.
        bool enabled = true;

        /// Verbosity from 0 (trace) to 6 (off).
        int verbosity = 2;
    };

    /// @brief Initializes file logging next to the loaded ASI.
    ///
    /// Creates (or truncates) @c AltairFix.log in the directory containing the
    /// supplied module, configures the shared spdlog format and level, and
    /// installs the logger as spdlog's default logger. Subsequent
    /// @c spdlog::info / @c spdlog::warn / @c spdlog::error calls from anywhere
    /// in the mod are routed to that file.
    ///
    /// The function is intentionally best-effort: a read-only game directory or
    /// a missing module path disables file logging but never prevents the mod
    /// from loading. Call it from @c DLL_PROCESS_ATTACH before starting worker
    /// threads that log.
    ///
    /// @param module Handle of the current ASI module, as supplied to
    ///               @c DllMain. It is used to resolve the ASI directory.
    /// @param options Configuration-driven logging settings.
    ///
    /// @return @c true when the logger is ready; @c false when logging is
    ///         disabled, set to off, or the log file could not be opened.
    ///
    /// @note Repeated calls replace the previous logger and truncate the log
    ///       again, which keeps a manual ASI reload consistent with a fresh
    ///       game launch.
    [[nodiscard]] bool Initialize(HMODULE module, const Options& options);

    /// @brief Flushes pending records and releases the log file.
    ///
    /// Call from @c DLL_PROCESS_DETACH. The function is safe to call when
    /// Initialize() failed or was never invoked.
    void Shutdown();
} // namespace AltairFix::Logging

#endif // ALTAIRFIX_LOGGING_LOGGING_H