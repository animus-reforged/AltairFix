#pragma once
#include <string>

// Configuration for display + misc options
struct Config {
	int windowMode = 0;     // 0=Normal, 1=Borderless, 2=Windowed
	int windowWidth = 1280;    // If 0 → use default
	int windowHeight = 720;
	int windowPosX = -1;    // -1=center
	int windowPosY = -1;    // -1=center
	bool highCoreCountFix = false;
	bool enableLogging = true;
	int logLevel = 2;
};

extern Config g_config;

void LoadConfig();