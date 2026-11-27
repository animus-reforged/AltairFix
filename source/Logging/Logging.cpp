#include "Logging/Logging.h"

#include "Paths/Paths.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <exception>
#include <filesystem>
#include <string>
#include <utility>

namespace
{
    /// Name used by spdlog's logger registry. Keeping it stable lets Initialize()
    /// replace a logger from a previous attach instead of failing with a
    /// duplicate-name exception.
    constexpr const char* LOGGER_NAME = "AltairFix";

    /// Log file placed beside the ASI. The name is fixed (not timestamped) so
    /// users and support tooling can find it without inspecting the directory.
    constexpr const wchar_t* LOG_FILE_NAME = L"AltairFix.log";

    /// One line per record: local time, severity, message.
    constexpr const char* LOG_PATTERN = "[%Y-%m-%d %H:%M:%S.%e] [%l] %v";

    /// @brief Maps the numeric configuration verbosity to an spdlog level.
    ///
    /// The numbering matches the legacy INI and the default AltairFix.toml:
    /// 0 trace, 1 debug, 2 info, 3 warning, 4 error, 5 critical, 6 off.
    ///
    /// @param verbosity Value from the configuration file.
    /// @return Matching spdlog level, or info for an unexpected value.
    [[nodiscard]] spdlog::level::level_enum ToSpdlogLevel(int verbosity)
    {
        switch (verbosity)
        {
        case 0:
            return spdlog::level::trace;
        case 1:
            return spdlog::level::debug;
        case 2:
            return spdlog::level::info;
        case 3:
            return spdlog::level::warn;
        case 4:
            return spdlog::level::err;
        case 5:
            return spdlog::level::critical;
        case 6:
            return spdlog::level::off;
        default:
            return spdlog::level::info;
        }
    }
} // namespace

namespace AltairFix::Logging
{
    bool Initialize(HMODULE module, const Options& options)
    {
        if (!options.enabled)
        {
            OutputDebugStringW(L"AltairFix: file logging is disabled by configuration.\n");
            return false;
        }

        if (options.verbosity >= 6)
        {
            OutputDebugStringW(L"AltairFix: logging verbosity is set to off.\n");
            return false;
        }

        const std::filesystem::path logPath = Paths::GetFileNextToModule(module, LOG_FILE_NAME);
        if (logPath.empty())
        {
            OutputDebugStringW(L"AltairFix: could not resolve the ASI path; file logging is disabled.\n");
            return false;
        }

        try
        {
            // Replace any logger left behind by an earlier attach. This also
            // makes tests and manual reloads deterministic.
            spdlog::drop(LOGGER_NAME);

            // truncate = true is what gives every launch a fresh log file.
            auto logger = spdlog::basic_logger_mt(LOGGER_NAME, logPath.wstring(), true);
            const spdlog::level::level_enum level = ToSpdlogLevel(options.verbosity);
            logger->set_level(level);
            logger->set_pattern(LOG_PATTERN);

            // Flush every record. Game processes can terminate without running
            // static destructors, and a lost crash context is worse than the
            // small write cost for this mod's low log volume.
            logger->flush_on(spdlog::level::trace);
            spdlog::set_default_logger(std::move(logger));

            spdlog::info("Logging initialized (spdlog {}.{}.{}, level {}).",
                         SPDLOG_VER_MAJOR,
                         SPDLOG_VER_MINOR,
                         SPDLOG_VER_PATCH,
                         spdlog::level::to_string_view(level));
            spdlog::info("Module: {}", Paths::ToUtf8(Paths::GetModulePath(module).wstring()));
            spdlog::info("Log file: {} (recreated for this launch).", Paths::ToUtf8(logPath.wstring()));
            return true;
        }
        catch (const std::exception& error)
        {
            // Logging must never prevent the mod from loading. A read-only or
            // missing directory falls back to no file output; the debugger can
            // still show the reason during development.
            std::string message = "AltairFix: file logging is unavailable: ";
            message += error.what();
            message += '\n';
            OutputDebugStringA(message.c_str());
            return false;
        }
    }

    void Shutdown()
    {
        // Drops the file sink and flushes pending records. spdlog recreates a
        // default logger on the next call, so late logging cannot crash.
        spdlog::shutdown();
    }
} // namespace AltairFix::Logging