#pragma once
#include "Constants.h"
#include <memory>
#include <spdlog/spdlog.h>

class Logger
{
public:
    // Initialize logger (call once at DLL startup)
    static void Init();

    // Access the logger anywhere
    static std::shared_ptr<spdlog::logger>& Get();

    // Shutdown logger (call at DLL unload)
    static void Shutdown();

    static void SetLevel(int level);
private:
    static std::shared_ptr<spdlog::logger> s_Logger;
};
