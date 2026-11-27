#include "Logging/Logging.h"

#include "Paths/Paths.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>
#include <winternl.h>

#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
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

    /// One line per record: local time, severity, process/thread, message.
    constexpr const char* LOG_PATTERN = "[%Y-%m-%d %H:%M:%S.%e] [%l] [%P:%t] %v";

    /// Time source origin for ElapsedMilliseconds().
    std::int64_t g_startedAtMs = 0;

    /// @brief Returns the current steady-clock time in milliseconds.
    std::int64_t NowMilliseconds()
    {
        return static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    /// @brief Logs the Windows version through ntdll's RtlGetVersion.
    void LogOsVersion()
    {
        using RtlGetVersionFn = LONG(WINAPI*)(RTL_OSVERSIONINFOW*);

        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (ntdll == nullptr)
        {
            return;
        }

        const auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
        if (rtlGetVersion == nullptr)
        {
            return;
        }

        RTL_OSVERSIONINFOW version{};
        version.dwOSVersionInfoSize = static_cast<ULONG>(sizeof(version));
        if (rtlGetVersion(&version) == 0)
        {
            spdlog::info("[core] os={}.{}.{}", version.dwMajorVersion, version.dwMinorVersion, version.dwBuildNumber);
        }
    }

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

            g_startedAtMs = NowMilliseconds();

            spdlog::info("[core] logging initialized level={} spdlog={}.{}.{}",
                         spdlog::level::to_string_view(level),
                         SPDLOG_VER_MAJOR,
                         SPDLOG_VER_MINOR,
                         SPDLOG_VER_PATCH);
            spdlog::info("[core] log file={} mode=truncate", Paths::ToUtf8(logPath.wstring()));
            spdlog::info("[core] game={} pid={} build={} {}",
                         Paths::ToUtf8(Paths::GetModulePath(nullptr).wstring()),
                         GetCurrentProcessId(),
                         __DATE__,
                         __TIME__);
            spdlog::info("[core] module base=0x{:X} path={}", reinterpret_cast<std::uintptr_t>(module), Paths::ToUtf8(Paths::GetModulePath(module).wstring()));
            LogOsVersion();
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

    std::int64_t ElapsedMilliseconds()
    {
        if (g_startedAtMs == 0)
        {
            return 0;
        }
        return NowMilliseconds() - g_startedAtMs;
    }

    std::string Win32ErrorMessage(DWORD error)
    {
        if (error == 0)
        {
            return "success";
        }

        const std::error_code code(static_cast<int>(error), std::system_category());
        const std::string message = code.message();
        return message.empty() ? "unknown error" : message;
    }

    void Shutdown()
    {
        // Drops the file sink and flushes pending records. spdlog recreates a
        // default logger on the next call, so late logging cannot crash.
        spdlog::shutdown();
    }
} // namespace AltairFix::Logging