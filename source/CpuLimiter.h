#pragma once
#include <windows.h>

class CpuLimiter
{
public:
	// Applies affinity mask for the first 31 cores
	static void ProcessAffinityLimiter();

private:
	
};