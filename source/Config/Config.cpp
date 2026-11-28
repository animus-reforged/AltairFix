#include "Config/Config.h"

#include "Paths/Paths.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <toml++/toml.hpp>
#include <utility>
#include <vector>

namespace
{
    /// Configuration file placed beside the ASI.
    constexpr const wchar_t* CONFIG_FILE_NAME = L"AltairFix.toml";

    /// Path resolved by @c Load, used by @c SetBool.
    std::filesystem::path g_configPath;

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
                   "renderer_crash_guard = true\n"
                   "\n"
                   "# Report at most 31 CPU cores to the game (32-core startup fix).\n"
                   "cpu_core_limit = true\n";
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

    /// @brief Replaces a configuration file through a temporary file.
    ///
    /// Writing beside the original and replacing it keeps a failed write from
    /// truncating a working configuration.
    ///
    /// @param path Destination file.
    /// @param content Complete UTF-8 file contents.
    /// @param error Receives a diagnostic when the write fails.
    /// @return @c true when the file was replaced.
    [[nodiscard]] bool ReplaceFile(const std::filesystem::path& path, const std::string& content, std::string& error)
    {
        std::filesystem::path temporary = path;
        temporary += L".tmp";

        std::ofstream stream(temporary, std::ios::trunc | std::ios::binary);
        if (!stream)
        {
            error = "Could not create the temporary AltairFix.toml.";
            return false;
        }

        stream.write(content.data(), static_cast<std::streamsize>(content.size()));
        stream.close();
        if (!stream)
        {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            error = "Could not write the temporary AltairFix.toml.";
            return false;
        }

        if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
        {
            const DWORD code = GetLastError();
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            error = "Could not replace AltairFix.toml (Windows error ";
            error += std::to_string(code);
            error += ").";
            return false;
        }

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

        if (const auto enabled = table["fixes"]["cpu_core_limit"].value<bool>())
        {
            settings.fixes.cpuCoreLimit = *enabled;
        }
    }

    /// @brief Trims leading and trailing spaces and tabs from a line.
    ///
    /// @param line Line to trim.
    /// @return Trimmed view into @p line.
    [[nodiscard]] std::string_view TrimLine(std::string_view line)
    {
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
        {
            line.remove_prefix(1);
        }
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t'))
        {
            line.remove_suffix(1);
        }
        return line;
    }

    /// @brief Returns whether a line starts a TOML table section.
    ///
    /// @param line Line to inspect.
    /// @return @c true for @c [table] and @c [[array]] headers.
    [[nodiscard]] bool IsTableHeader(std::string_view line)
    {
        line = TrimLine(line);
        return line.starts_with('[');
    }

    /// @brief Returns whether a line declares the named table.
    ///
    /// @param line Line to inspect.
    /// @param table Table name without brackets.
    /// @return @c true when the line is the table's header.
    [[nodiscard]] bool IsTableHeaderFor(std::string_view line, std::string_view table)
    {
        line = TrimLine(line);
        if (!line.starts_with('['))
        {
            return false;
        }

        const std::string header = "[" + std::string(table) + "]";
        if (!line.starts_with(header))
        {
            return false;
        }

        return line.size() == header.size() || line[header.size()] == ' ' || line[header.size()] == '\t' || line[header.size()] == '#';
    }

    /// @brief Returns whether a line is blank or a TOML comment.
    ///
    /// @param line Line to inspect.
    /// @return @c true when the line has no setting content.
    [[nodiscard]] bool IsBlankOrComment(std::string_view line)
    {
        line = TrimLine(line);
        return line.empty() || line.starts_with('#');
    }

    /// A setting that can be materialized into an existing configuration file.
    struct KnownSetting
    {
        /// Table that owns the key.
        std::string_view table;

        /// Key name within the table.
        std::string_view key;

        /// Comment written above the key when it is added.
        std::string_view comment;

        /// TOML literal for the default value.
        std::string value;
    };

    /// Missing settings grouped by their owning table.
    struct MissingGroup
    {
        /// Table that owns the keys.
        std::string_view table;

        /// Whether the parsed document already contains the table.
        bool tableExists = false;

        /// Settings missing from the table.
        std::vector<const KnownSetting*> settings;
    };

    /// @brief Describes every setting known to this build and its default.
    ///
    /// Keep this list in sync with @c ApplyTable and @c BuildDefaultFileContent.
    ///
    /// @return Known settings in the order they should be written.
    [[nodiscard]] std::vector<KnownSetting> BuildKnownSettings()
    {
        return {
            {
                .table = "metadata",
                .key = "version",
                .comment = "# Version of AltairFix that wrote this file.",
                .value = std::string("\"") + ALTAIRFIX_VERSION + "\"",
            },
            {
                .table = "debug",
                .key = "enable_logging",
                .comment = "# Write AltairFix.log next to the ASI.",
                .value = AltairFix::Config::DEFAULT_ENABLE_LOGGING ? "true" : "false",
            },
            {
                .table = "debug",
                .key = "log_level",
                .comment = "# 0 = Trace, 1 = Debug, 2 = Info, 3 = Warning, 4 = Error, 5 = Critical, 6 = Off",
                .value = std::to_string(AltairFix::Config::DEFAULT_LOG_LEVEL),
            },
            {
                .table = "fixes",
                .key = "renderer_crash_guard",
                .comment = "# Guard the known retail DX9 renderer cleanup crash.",
                .value = AltairFix::Config::DEFAULT_RENDERER_CRASH_GUARD ? "true" : "false",
            },
            {
                .table = "fixes",
                .key = "cpu_core_limit",
                .comment = "# Report at most 31 CPU cores to the game (32-core startup fix).",
                .value = AltairFix::Config::DEFAULT_CPU_CORE_LIMIT ? "true" : "false",
            },
        };
    }

    /// @brief Splits raw TOML text into lines without line endings.
    ///
    /// @param content Raw file text.
    /// @return Lines in file order.
    [[nodiscard]] std::vector<std::string> SplitLines(const std::string& content)
    {
        std::vector<std::string> lines;
        std::size_t start = 0;
        while (start <= content.size())
        {
            const std::size_t end = content.find('\n', start);
            const std::size_t length = (end == std::string::npos ? content.size() : end) - start;
            std::string line = content.substr(start, length);
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            lines.push_back(std::move(line));
            if (end == std::string::npos)
            {
                break;
            }
            start = end + 1;
        }
        return lines;
    }

    /// @brief Builds the comment and assignment lines for missing settings.
    ///
    /// @param settings Missing settings in write order.
    /// @return Lines to insert.
    [[nodiscard]] std::vector<std::string> BuildSettingBlock(const std::vector<const KnownSetting*>& settings)
    {
        std::vector<std::string> block;
        for (std::size_t i = 0; i < settings.size(); ++i)
        {
            if (i > 0)
            {
                block.emplace_back();
            }
            block.emplace_back(settings[i]->comment);
            block.emplace_back(std::string(settings[i]->key) + " = " + settings[i]->value);
        }
        return block;
    }

    /// @brief Appends a missing table with all of its settings.
    ///
    /// @param lines File lines to extend.
    /// @param group Missing table and settings.
    void AppendMissingTable(std::vector<std::string>& lines, const MissingGroup& group)
    {
        if (!lines.empty() && !TrimLine(lines.back()).empty())
        {
            lines.emplace_back();
        }

        lines.emplace_back("[" + std::string(group.table) + "]");
        const std::vector<std::string> block = BuildSettingBlock(group.settings);
        lines.insert(lines.end(), block.begin(), block.end());
    }

    /// @brief Finds the header line of an existing table.
    ///
    /// @param lines File lines to search.
    /// @param table Table name without brackets.
    /// @return Header index, or @c std::nullopt when absent.
    [[nodiscard]] std::optional<std::size_t> FindTableHeader(const std::vector<std::string>& lines, std::string_view table)
    {
        for (std::size_t i = 0; i < lines.size(); ++i)
        {
            if (IsTableHeaderFor(lines[i], table))
            {
                return i;
            }
        }
        return std::nullopt;
    }

    /// @brief Finds the first line after a table section.
    ///
    /// @param lines File lines to search.
    /// @param headerIndex Index of the table header.
    /// @return Index of the next table header, or the line count.
    [[nodiscard]] std::size_t FindSectionEnd(const std::vector<std::string>& lines, std::size_t headerIndex)
    {
        for (std::size_t i = headerIndex + 1; i < lines.size(); ++i)
        {
            if (IsTableHeader(lines[i]))
            {
                return i;
            }
        }
        return lines.size();
    }

    /// @brief Finds where new keys should be inserted in a table section.
    ///
    /// @param lines File lines to search.
    /// @param headerIndex Index of the table header.
    /// @param sectionEnd Index of the next table header or the line count.
    /// @return Insertion index after the last key line.
    [[nodiscard]] std::size_t FindInsertionPoint(const std::vector<std::string>& lines, std::size_t headerIndex, std::size_t sectionEnd)
    {
        std::size_t insertAt = headerIndex + 1;
        for (std::size_t i = headerIndex + 1; i < sectionEnd; ++i)
        {
            if (!IsBlankOrComment(lines[i]))
            {
                insertAt = i + 1;
            }
        }
        return insertAt;
    }

    /// @brief Inserts missing keys into an existing table section.
    ///
    /// @param lines File lines to edit.
    /// @param group Missing table and settings.
    /// @return @c false when the table header was not found.
    [[nodiscard]] bool InsertMissingTableKeys(std::vector<std::string>& lines, const MissingGroup& group)
    {
        const std::optional<std::size_t> headerIndex = FindTableHeader(lines, group.table);
        if (!headerIndex)
        {
            return false;
        }

        const std::size_t sectionEnd = FindSectionEnd(lines, *headerIndex);
        const std::size_t insertAt = FindInsertionPoint(lines, *headerIndex, sectionEnd);
        std::vector<std::string> block = BuildSettingBlock(group.settings);
        if (insertAt > 0 && !TrimLine(lines[insertAt - 1]).empty())
        {
            block.insert(block.begin(), "");
        }
        lines.insert(lines.begin() + static_cast<std::vector<std::string>::difference_type>(insertAt), block.begin(), block.end());
        return true;
    }

    /// @brief Adds known settings missing from an existing configuration file.
    ///
    /// The raw file text is edited instead of being serialized from the parsed
    /// table, so comments, unknown keys, and user formatting are preserved.
    /// Missing keys are inserted at the end of their table section; missing
    /// tables are appended.
    ///
    /// @param content Raw file text. Updated when settings are added.
    /// @param document Parsed configuration document.
    /// @param warning Receives a non-fatal problem when a table cannot be edited.
    /// @return @c true when @p content changed.
    [[nodiscard]] bool AddMissingDefaults(std::string& content, const toml::table& document, std::string& warning)
    {
        const std::vector<KnownSetting> knownSettings = BuildKnownSettings();

        std::vector<MissingGroup> groups;
        for (const KnownSetting& setting : knownSettings)
        {
            const auto tableNode = document[setting.table];
            const bool tableExists = static_cast<bool>(tableNode);
            const toml::table* table = tableNode.as_table();
            if (tableExists && table == nullptr)
            {
                AppendWarning(warning, "Cannot add missing " + std::string(setting.table) + "." + std::string(setting.key) + ": it is not a TOML table.");
                continue;
            }

            if (table != nullptr && table->contains(setting.key))
            {
                continue;
            }

            auto group = std::ranges::find_if(groups, [&setting](const MissingGroup& candidate) { return candidate.table == setting.table; });
            if (group == groups.end())
            {
                groups.push_back({.table = setting.table, .tableExists = tableExists, .settings = {}});
                group = groups.end() - 1;
            }
            group->settings.push_back(&setting);
        }

        if (groups.empty())
        {
            return false;
        }

        const bool crlf = content.contains("\r\n");
        std::vector<std::string> lines = SplitLines(content);
        if (std::ranges::all_of(lines, [](const std::string& line) { return TrimLine(line).empty(); }))
        {
            lines.clear();
        }

        bool changed = false;
        for (const MissingGroup& group : groups)
        {
            if (!group.tableExists)
            {
                AppendMissingTable(lines, group);
                changed = true;
                continue;
            }

            if (!InsertMissingTableKeys(lines, group))
            {
                AppendWarning(warning, "Cannot add missing [" + std::string(group.table) + "] keys: its table header was not found.");
                continue;
            }
            changed = true;
        }

        if (!changed)
        {
            return false;
        }

        while (!lines.empty() && TrimLine(lines.back()).empty())
        {
            lines.pop_back();
        }

        std::string updated;
        const std::string newline = crlf ? "\r\n" : "\n";
        for (const std::string& line : lines)
        {
            updated += line;
            updated += newline;
        }
        content = std::move(updated);
        return true;
    }
    /// @brief Replaces or appends a boolean key inside an existing table.
    ///
    /// @param lines File lines to edit.
    /// @param headerIndex Index of the table header.
    /// @param key Key to update.
    /// @param assignment Complete assignment line to write.
    void SetBoolInTable(std::vector<std::string>& lines, std::size_t headerIndex, std::string_view key, const std::string& assignment)
    {
        const std::size_t sectionEnd = FindSectionEnd(lines, headerIndex);
        for (std::size_t i = headerIndex + 1; i < sectionEnd; ++i)
        {
            const std::string_view line = TrimLine(lines[i]);
            const std::size_t equals = line.find('=');
            if (equals == std::string_view::npos || TrimLine(line.substr(0, equals)) != key)
            {
                continue;
            }

            lines[i] = assignment;
            return;
        }

        std::size_t insertAt = headerIndex + 1;
        for (std::size_t i = headerIndex + 1; i < sectionEnd; ++i)
        {
            if (!IsBlankOrComment(lines[i]))
            {
                insertAt = i + 1;
            }
        }

        if (insertAt > 0 && !TrimLine(lines[insertAt - 1]).empty())
        {
            lines.insert(lines.begin() + static_cast<std::vector<std::string>::difference_type>(insertAt), "");
            ++insertAt;
        }
        lines.insert(lines.begin() + static_cast<std::vector<std::string>::difference_type>(insertAt), assignment);
    }
} // namespace

namespace AltairFix::Config
{
    LoadResult Load(HMODULE module)
    {
        LoadResult result;
        result.path = Paths::GetFileNextToModule(module, CONFIG_FILE_NAME);
        g_configPath = result.path;

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

                std::string updatedContent = content;
                if (AddMissingDefaults(updatedContent, table, result.warning))
                {
                    std::string writeError;
                    if (ReplaceFile(result.path, updatedContent, writeError))
                    {
                        result.addedMissingDefaults = true;
                    }
                    else
                    {
                        AppendWarning(result.warning, writeError);
                    }
                }
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

    bool SetBool(std::string_view table, std::string_view key, bool value)
    {
        if (g_configPath.empty())
        {
            return false;
        }

        std::string content;
        std::string error;
        if (!ReadUtf8File(g_configPath, content, error))
        {
            return false;
        }

        try
        {
            (void)toml::parse(content, Paths::ToUtf8(g_configPath.wstring()));
        }
        catch (const std::exception&)
        {
            return false;
        }

        const bool crlf = content.contains("\r\n");
        std::vector<std::string> lines = SplitLines(content);
        const std::string assignment = std::string(key) + " = " + (value ? "true" : "false");

        const std::optional<std::size_t> headerIndex = FindTableHeader(lines, table);
        if (!headerIndex)
        {
            if (!lines.empty() && !TrimLine(lines.back()).empty())
            {
                lines.emplace_back();
            }
            lines.emplace_back("[" + std::string(table) + "]");
            lines.emplace_back(assignment);
        }
        else
        {
            SetBoolInTable(lines, *headerIndex, key, assignment);
        }

        std::string updated;
        const std::string newline = crlf ? "\r\n" : "\n";
        for (const std::string& line : lines)
        {
            updated += line;
            updated += newline;
        }
        return ReplaceFile(g_configPath, updated, error);
    }
} // namespace AltairFix::Config