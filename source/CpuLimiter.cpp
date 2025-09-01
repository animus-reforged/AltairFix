#include "CpuLimiter.h"
#include "Logger.h"

void CpuLimiter::ProcessAffinityLimiter()
{
	Logger::Get()->info("Applying CPU affinity limit...");
	HANDLE hProcess = GetCurrentProcess();

	DWORD_PTR processMask = 0, systemMask = 0;
	if (!GetProcessAffinityMask(hProcess, &processMask, &systemMask)) {
		Logger::Get()->error("GetProcessAffinityMask failed, error={}", GetLastError());
		return;
	}

	// Count how many logical CPUs are in this mask
	int availableCpus = 0;
	for (DWORD_PTR bit = 1; bit != 0; bit <<= 1) {
		if (systemMask & bit) availableCpus++;
	}

	if (availableCpus > 64) {
		Logger::Get()->warn("System has >64 logical processors (multi-group). Skipping CPU limit.");
		return;
	}

	int limit = (availableCpus > 32) ? 31 : availableCpus;
	Logger::Get()->info("System has {} logical processors, limiting to {}", availableCpus, limit);

	DWORD_PTR newMask = 0;
	int count = 0;
	for (DWORD_PTR bit = 1; bit != 0 && count < limit; bit <<= 1) {
		if (systemMask & bit) {
			newMask |= bit;
			count++;
		}
	}

	if (!SetProcessAffinityMask(hProcess, newMask)) {
		Logger::Get()->error("Failed to set affinity, error={}", GetLastError());
	}
	else {
		Logger::Get()->info("Process affinity limited to {} logical processors", limit);
	}
}