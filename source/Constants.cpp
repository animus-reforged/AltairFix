#include "Constants.h"

namespace Constants {
    const std::string FixName = "AltairFix";
    const std::string FixLogFile = FixName + ".log";
    const std::string FixConfigFile = ".\\" + FixName + ".ini";
    const std::string FixVersion = "0.2.0";

    std::filesystem::path BasePath = std::filesystem::current_path();
    std::filesystem::path FullLogFilePath = BasePath / FixLogFile;
}