#include "Config.h"
#include "Logger.h"
#include "Constants.h"
#include <windows.h>
#include <string>

Config g_config;

void LoadConfig()
{
	const char* configFile = Constants::FixConfigFile.c_str();
	Logger::Get()->info("Loading configuration file {}", Constants::FixConfigFile);

	// Metadata
	char buf[64] = { 0 };
	GetPrivateProfileStringA("Metadata", "Version", "1.0.0", buf, sizeof(buf), configFile);
	g_config.version = std::string(buf);

	// Display settings
	g_config.windowMode = GetPrivateProfileIntA("Display", "WindowMode", 0, configFile);
	g_config.windowWidth = GetPrivateProfileIntA("Display", "WindowWidth", GetSystemMetrics(SM_CXSCREEN) / 2, configFile);
	g_config.windowHeight = GetPrivateProfileIntA("Display", "WindowHeight", GetSystemMetrics(SM_CYSCREEN) / 2, configFile);
	g_config.windowPosX = GetPrivateProfileIntA("Display", "WindowPosX", -1, configFile);
	g_config.windowPosY = GetPrivateProfileIntA("Display", "WindowPosY", -1, configFile);

	// Tweaks
	g_config.highCoreCountFix = GetPrivateProfileIntA("Tweaks", "HighCoreCountFix", 0, configFile) != 0;
	g_config.serverBlocker = GetPrivateProfileIntA("Tweaks", "ServerBlocker", 0, configFile) != 0;

	// Debug
	g_config.enableLogging = GetPrivateProfileIntA("Debug", "EnableLogging", 1, configFile) != 0;
	g_config.logLevel = GetPrivateProfileIntA("Debug", "LogLevel", 2, configFile);
	if (g_config.logLevel < 0 || g_config.logLevel > 6) {
		g_config.logLevel = 2; // default info
	}
	Logger::SetLevel(g_config.logLevel);
	if (g_config.windowMode < 0 || g_config.windowMode > 2)
	{
		g_config.windowMode = 0;
	}

	Logger::Get()->info("Configuration loaded:");
	Logger::Get()->info("  WindowMode: " + std::to_string(g_config.windowMode) +
		" (" + (g_config.windowMode == 0 ? "Normal" :
			g_config.windowMode == 1 ? "Borderless" : "Windowed") + ")");
	if (g_config.windowMode == 2) {
		Logger::Get()->debug("  Window Size: " + std::to_string(g_config.windowWidth) +
			"x" + std::to_string(g_config.windowHeight));
		Logger::Get()->debug("  Window Pos: " + std::to_string(g_config.windowPosX) +
			"," + std::to_string(g_config.windowPosY));
	}
}