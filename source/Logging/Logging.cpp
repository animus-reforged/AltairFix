#include "Logging/Logging.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <exception>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

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

#ifdef NDEBUG
    /// Release builds keep the log focused on lifecycle and failures.
    constexpr spdlog::level::level_enum DEFAULT_LOG_LEVEL = spdlog::level::info;
#else
    /// Debug builds also record development diagnostics.
    constexpr spdlog::level::level_enum DEFAULT_LOG_LEVEL = spdlog::level::debug;
#endif

    /// @brief Returns the full path of the executable image that contains the
    ///        given module handle.
    ///
    /// The buffer starts at MAX_PATH and doubles until GetModuleFileNameW()
    /// reports a length that fits. Since Windows Vista, a truncated call fills
    /// the buffer exactly and omits the null terminator, which makes the
    /// "length equals capacity" case ambiguous; retrying with more room is the
    /// documented way to distinguish truncation from an exact fit.
    ///
    /// @param module Module whose image path should be resolved.
    /// @return Absolute module path, or an empty path if Windows cannot resolve
    ///         the handle.
    [[nodiscard]] std::filesystem::path GetModulePath(HMODULE module)
    {
        std::vector<wchar_t> buffer(MAX_PATH);
        for (;;)
        {
            const DWORD length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0)
            {
                return {};
            }

            if (length < buffer.size())
            {
                return {std::wstring(buffer.data(), length)};
            }

            buffer.resize(buffer.size() * 2);
        }
    }

    /// @brief Converts wide text to UTF-8 for spdlog's narrow message strings.
    ///
    /// spdlog messages remain @c std::string even when wide filenames are
    /// enabled, so paths are converted explicitly before they are formatted
    /// into log records.
    ///
    /// @param text Wide text to convert.
    /// @return UTF-8 representation, or an empty string when conversion fails.
    [[nodiscard]] std::string ToUtf8(const std::wstring& text)
    {
        if (text.empty())
        {
            return {};
        }

        const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (size <= 0)
        {
            return {};
        }

        std::string result(static_cast<size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
        return result;
    }

    /// @brief Builds the log path that sits next to the supplied ASI module.
    ///
    /// @param module Module whose directory should receive the log file.
    /// @return @c <asi-directory>/AltairFix.log, or an empty path when the
    ///         module path cannot be resolved.
    [[nodiscard]] std::filesystem::path GetLogPath(HMODULE module)
    {
        const std::filesystem::path modulePath = GetModulePath(module);
        if (modulePath.empty())
        {
            return {};
        }

        return modulePath.parent_path() / LOG_FILE_NAME;
    }
} // namespace

namespace AltairFix::Logging
{
    bool Initialize(HMODULE module)
    {
        const std::filesystem::path logPath = GetLogPath(module);
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
            logger->set_level(DEFAULT_LOG_LEVEL);
            logger->set_pattern(LOG_PATTERN);

            // Flush every record. Game processes can terminate without running
            // static destructors, and a lost crash context is worse than the
            // small write cost for this mod's low log volume.
            logger->flush_on(spdlog::level::trace);
            spdlog::set_default_logger(std::move(logger));

            spdlog::info("Logging initialized (spdlog {}.{}.{}).", SPDLOG_VER_MAJOR, SPDLOG_VER_MINOR, SPDLOG_VER_PATCH);
            spdlog::info("Module: {}", ToUtf8(GetModulePath(module).wstring()));
            spdlog::info("Log file: {} (recreated for this launch).", ToUtf8(logPath.wstring()));
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