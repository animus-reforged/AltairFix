#pragma once
#include <string>
#include <filesystem>

namespace Constants {
    extern const std::string FixName;
    extern const std::string FixLogFile;
    extern const std::string FixConfigFile;

    extern std::filesystem::path BasePath;
    extern std::filesystem::path FullLogFilePath;
}