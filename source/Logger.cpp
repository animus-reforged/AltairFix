#include "Logger.h"
#include <spdlog/sinks/rotating_file_sink.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <windows.h> 
#include "Config.h"

// Define static member
std::shared_ptr<spdlog::logger> Logger::s_Logger = nullptr;

void Logger::Init()
{
	if (s_Logger)
		return; // already initialized

	try
	{
		// Ensure log directory exists
		if (!std::filesystem::exists(Constants::BasePath))
			std::filesystem::create_directories(Constants::BasePath);

		// Truncate existing log file
		std::ofstream file(Constants::FullLogFilePath, std::ios::trunc);
		if (file.is_open())
			file.close();

		// Create rotating logger: max 10 MB, 1 file
		s_Logger = std::make_shared<spdlog::logger>(
			Constants::FixName,
			std::make_shared<spdlog::sinks::rotating_file_sink_st>(
				Constants::FullLogFilePath.string(), 10 * 1024 * 1024, 1)
		);

		spdlog::set_default_logger(s_Logger);
		spdlog::flush_on(spdlog::level::debug);
		SetLevel(g_config.logLevel);
	}
	catch (const spdlog::spdlog_ex& ex)
	{
		AllocConsole();
		FILE* dummy;
		freopen_s(&dummy, "CONOUT$", "w", stdout);
		std::cout << "Log initialization failed: " << ex.what() << std::endl;
	}
}

std::shared_ptr<spdlog::logger>& Logger::Get()
{
	return s_Logger;
}

void Logger::SetLevel(int level)
{
	if (!s_Logger) return;

	spdlog::level::level_enum lvl;
	switch (level) {
	case 0: lvl = spdlog::level::trace; break;
	case 1: lvl = spdlog::level::debug; break;
	case 2: lvl = spdlog::level::info; break;
	case 3: lvl = spdlog::level::warn; break;
	case 4: lvl = spdlog::level::err; break;
	case 5: lvl = spdlog::level::critical; break;
	case 6: lvl = spdlog::level::off; break;
	default: lvl = spdlog::level::info; break;
	}

	spdlog::set_level(lvl);
	s_Logger->info("Log level changed to {}", level);
}

void Logger::Shutdown()
{
	spdlog::shutdown();
	s_Logger.reset();
}