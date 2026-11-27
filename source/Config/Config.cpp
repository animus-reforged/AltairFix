#include "Config/Config.h"

#include "Paths/Paths.h"

#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <toml++/toml.hpp>

namespace
{
    /// Configuration file placed beside the ASI.
    constexpr const wchar_t* CONFIG_FILE_NAME = L"AltairFix.toml";

    /// @brief Builds the contents written when @c AltairFix.toml is missing.
    ///
    /// The file is deliberately self-describing: every key carries its valid
    /// range or meaning so users do not need the README to edit it.
    ///
    /// @return Complete TOML document with default values.
    [[nodiscard]] std::string BuildDefaultFileContent()
    {
        std::string content = "# AltairFix configuration file.\n"
                              "# Created with defaults when missing; missing keys fall back to these values.\n"
                              "\n"
                              "[metadata]\n"
                              "# Version of AltairFix that wrote this file.\n"
                              "version = \"";
        content += ALTAIRFIX_VERSION;
        content += "\"\n"
                   "\n"
                   "[debug]\n"
                   "# Write AltairFix.log next to the ASI.\n"
                   "enable_logging = true\n"
                   "\n"
                   "# 0 = Trace, 1 = Debug, 2 = Info, 3 = Warning, 4 = Error, 5 = Critical, 6 = Off\n"
                   "log_level = 2\n"
                   "\n"
                   "[fixes]\n"
                   "# Guard the known retail DX9 renderer cleanup crash.\n"
                   "renderer_crash_guard = true\n";
        return content;
    }

    /// @brief Appends a non-fatal problem to the warning text.
    ///
    /// Multiple problems are kept on one logical message so the caller can log
    /// them after spdlog has been initialized.
    ///
    /// @param warning Warning text to extend.
    /// @param message Problem to append.
    void AppendWarning(std::string& warning, std::string_view message)
    {
        if (!warning.empty())
        {
            warning += ' ';
        }

        warning += message;
    }

    /// @brief Reads a UTF-8 text file through a native wide Windows path.
    ///
    /// Narrow C++ file streams interpret paths with the active ANSI code page,
    /// which breaks installations under non-ASCII directories. CreateFileW uses
    /// the wide path directly. The size limit rejects accidental giant files
    /// before allocating memory.
    ///
    /// @param path File to read.
    /// @param content Receives the complete file contents.
    /// @param error Receives a diagnostic when the read fails.
    /// @return @c true when the file was read completely.
    [[nodiscard]] bool ReadUtf8File(const std::filesystem::path& path, std::string& content, std::string& error)
    {
        constexpr LONGLONG MAX_CONFIG_BYTES = 16LL * 1024 * 1024;

        HANDLE file = CreateFileW(path.c_str(),
                                  GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr,
                                  OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            error = "Could not open AltairFix.toml (Windows error ";
            error += std::to_string(GetLastError());
            error += ").";
            return false;
        }

        LARGE_INTEGER size{};
        if (GetFileSizeEx(file, &size) == 0 || size.QuadPart < 0 || size.QuadPart > MAX_CONFIG_BYTES)
        {
            CloseHandle(file);
            error = "AltairFix.toml has an invalid size.";
            return false;
        }

        content.resize(static_cast<size_t>(size.QuadPart));
        size_t totalRead = 0;
        while (totalRead < content.size())
        {
            DWORD read = 0;
            const auto remaining = static_cast<DWORD>(content.size() - totalRead);
            if (ReadFile(file, content.data() + totalRead, remaining, &read, nullptr) == 0 || read == 0)
            {
                CloseHandle(file);
                error = "Could not read AltairFix.toml.";
                return false;
            }

            totalRead += read;
        }

        CloseHandle(file);
        return true;
    }

    /// @brief Sends a warning to the debugger immediately.
    ///
    /// Configuration is loaded before file logging exists, and logging can be
    /// disabled entirely. OutputDebugString keeps the problem visible in a
    /// debugger in both cases.
    ///
    /// @param warning Warning text to report.
    void ReportWarning(const std::string& warning)
    {
        if (warning.empty())
        {
            return;
        }

        std::string line = warning;
        line += '\n';
        OutputDebugStringA(line.c_str());
    }

    /// @brief Applies the parsed TOML values to the settings.
    ///
    /// Missing tables or keys are ignored, leaving the corresponding default
    /// in place. Out-of-range values are reported through @p warning.
    ///
    /// @param table Parsed TOML document.
    /// @param settings Settings to update.
    /// @param warning Receives non-fatal validation messages.
    void ApplyTable(const toml::table& table, AltairFix::Config::Settings& settings, std::string& warning)
    {
        if (const auto version = table["metadata"]["version"].value<std::string>())
        {
            if (!version->empty())
            {
                settings.metadata.version = *version;
            }
        }

        if (const auto enabled = table["debug"]["enable_logging"].value<bool>())
        {
            settings.debug.enableLogging = *enabled;
        }

        if (const auto level = table["debug"]["log_level"].value<int64_t>())
        {
            if (*level < AltairFix::Config::MIN_LOG_LEVEL || *level > AltairFix::Config::MAX_LOG_LEVEL)
            {
                AppendWarning(warning, "debug.log_level must be between 0 and 6; using the default of 2.");
            }
            else
            {
                settings.debug.logLevel = static_cast<int>(*level);
            }
        }

        if (const auto enabled = table["fixes"]["renderer_crash_guard"].value<bool>())
        {
            settings.fixes.rendererCrashGuard = *enabled;
        }
    }
} // namespace

namespace AltairFix::Config
{
    LoadResult Load(HMODULE module)
    {
        LoadResult result;
        result.path = Paths::GetFileNextToModule(module, CONFIG_FILE_NAME);

        if (result.path.empty())
        {
            result.warning = "Could not resolve the ASI directory; using default settings.";
            ReportWarning(result.warning);
            return result;
        }

        std::error_code error;
        const bool exists = std::filesystem::exists(result.path, error);
        if (error)
        {
            result.warning = "Could not inspect AltairFix.toml: ";
            result.warning += error.message();
            result.warning += " Using default settings.";
            ReportWarning(result.warning);
            return result;
        }

        if (!exists)
        {
            std::ofstream stream(result.path, std::ios::trunc);
            if (!stream)
            {
                result.warning = "Could not create the default AltairFix.toml; using default settings.";
            }
            else
            {
                stream << BuildDefaultFileContent();
                stream.close();

                if (!stream)
                {
                    // Do not leave a half-written file behind: the next launch
                    // would otherwise try to parse an incomplete document.
                    std::filesystem::remove(result.path, error);
                    result.warning = "Could not write the default AltairFix.toml; using default settings.";
                }
                else
                {
                    result.createdDefaultFile = true;
                }
            }

            ReportWarning(result.warning);
            return result;
        }

        std::string content;
        std::string fileError;
        if (!ReadUtf8File(result.path, content, fileError))
        {
            result.warning = fileError;
            result.warning += " Using default settings.";
        }
        else
        {
            try
            {
                const toml::table table = toml::parse(content, Paths::ToUtf8(result.path.wstring()));
                ApplyTable(table, result.settings, result.warning);
            }
            catch (const toml::parse_error& parseError)
            {
                result.warning = "Could not parse AltairFix.toml: ";
                result.warning += parseError.what();
                result.warning += " Using default settings.";
            }
            catch (const std::exception& readError)
            {
                result.warning = "Could not read AltairFix.toml: ";
                result.warning += readError.what();
                result.warning += " Using default settings.";
            }
        }

        ReportWarning(result.warning);
        return result;
    }
} // namespace AltairFix::Config