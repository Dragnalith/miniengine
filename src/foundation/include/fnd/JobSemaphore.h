#pragma once

#include <fnd/Util.h>
#include <fnd/Job.h>
#include <fnd/SpinLock.h>
#include <atomic>
#include <functional>

namespace engine
{

class JobSemaphore
{
public:
	JobSemaphore();
	int Acquire();
	int Release();
private:
	SpinLock m_lock;
	int m_counter = 0;
	JobCounter m_event;
};

}