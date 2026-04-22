#pragma once

#include <windows.h>

namespace fnd
{
	bool IsProfilingEnabled();
	void SetThreadName(const char* name);
}

#define PROFILE_SET_THREADNAME(name)
#define PROFILE_SCOPE(ID)
#define PROFILE_SCOPE_COLOR(ID, r, g, b)
#define PROFILE_SCOPE_DATA_COLOR(ID, data, r, g, b)
#define PROFILE_JOB_NAME(name)

#define PROFILE_DEFAULT_FRAME
#define PROFILE_FRAME(name)

#define PROFILE_REGISTER_FIBER(Fiber, Name)
#define PROFILE_UNREGISTER_FIBER(Fiber, Name)
#define SWITCH_TO_FIBER(Fiber, Name) ::SwitchToFiber((Fiber));
