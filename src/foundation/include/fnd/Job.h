#pragma once

#include <fnd/Util.h>
#include <fnd/SpinLock.h>
#include <atomic>
#include <functional>
#include <source_location>


namespace engine
{

class JobCounter;

struct Job
{
	static void Dispatch(const char* name, JobCounter& handle, std::function<void()> job);
	static void Wait(JobCounter& handle, int64_t value, int64_t reset, const std::source_location location = std::source_location::current());
	static void Wait(JobCounter& handle, int64_t value, const std::source_location location = std::source_location::current());
	static void Wait(JobCounter& handle, const std::source_location location = std::source_location::current());
	static void YieldJob();
};

class JobCounter {
	friend class FiberJob;
	friend class JobQueue;
public:
	void Set(int64_t value) {
		m_counter.store(value);
	}
	int64_t Get() const {
		return m_counter.load();
	}
	void Add(int64_t value) {
		m_counter.fetch_add(value);
	}
	void Sub(int64_t value) {
		m_counter.fetch_sub(value);
	}
protected:
	uint64_t m_test = 0xdeadbeef;
	std::atomic<int64_t> m_counter = 0;
};

}