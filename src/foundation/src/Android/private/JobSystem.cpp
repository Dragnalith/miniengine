#include <private/JobSystem.h>

#include <fnd/Job.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

// Android job system. bionic has no ucontext fiber primitives, so jobs run on
// real OS threads scheduled by the kernel rather than stackful fibers on a
// fixed worker pool.
//
// Threads are pooled and reused: Dispatch hands a job to an idle thread, and
// only spawns a new thread when every pooled thread is busy. Crucially Dispatch
// never *queues* a job behind a busy thread -- if a job calls Job::Wait it
// blocks its own thread, and any child it dispatches always gets a thread
// (reused or freshly grown), so the "all workers blocked" deadlock of a bounded
// queue-based pool cannot happen.
//
// Job::Wait blocks on the target counter via C++20 atomic wait/notify and keeps
// the JobCounter compare-exchange(value -> reset) contract of the Win32 fiber
// backend.

namespace migi
{

namespace
{

class ThreadPool
{
public:
    void Dispatch(std::function<void()> job)
    {
        Worker* worker = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_idle.empty())
            {
                worker = m_idle.back();
                m_idle.pop_back();
            }
            else
            {
                worker = new Worker(this);
                m_all.push_back(worker);
                worker->thread = std::thread([worker] { worker->Run(); });
            }
        }
        worker->Assign(std::move(job));
    }

    // Called once all jobs have drained: stop and join every pooled thread.
    void Shutdown()
    {
        std::vector<Worker*> all;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            all.swap(m_all);
            m_idle.clear();
        }
        for (Worker* worker : all)
        {
            worker->Stop();
        }
        for (Worker* worker : all)
        {
            worker->thread.join();
            delete worker;
        }
    }

private:
    struct Worker
    {
        explicit Worker(ThreadPool* owner)
            : pool(owner)
        {
        }

        void Assign(std::function<void()> job)
        {
            {
                std::lock_guard<std::mutex> lock(mutex);
                task = std::move(job);
                hasTask = true;
            }
            cv.notify_one();
        }

        void Stop()
        {
            {
                std::lock_guard<std::mutex> lock(mutex);
                stop = true;
            }
            cv.notify_one();
        }

        void Run()
        {
            for (;;)
            {
                std::function<void()> job;
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    cv.wait(lock, [this] { return hasTask || stop; });
                    if (stop && !hasTask)
                    {
                        return;
                    }
                    job = std::move(task);
                    hasTask = false;
                }
                job();
                pool->ReturnToIdle(this);
            }
        }

        ThreadPool* pool;
        std::thread thread;
        std::mutex mutex;
        std::condition_variable cv;
        std::function<void()> task;
        bool hasTask = false;
        bool stop = false;
    };

    void ReturnToIdle(Worker* worker)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_idle.push_back(worker);
    }

    std::mutex m_mutex;
    std::vector<Worker*> m_idle;
    std::vector<Worker*> m_all;
};

// Active pool for the duration of JobSystem::Start, and the count of jobs
// dispatched but not yet finished (drained to zero before shutdown, mirroring
// the fiber scheduler's RemainingJobCount() == 0).
ThreadPool* g_pool = nullptr;
std::atomic<int64_t> g_outstandingJobs{0};

} // namespace

void Job::Dispatch(const char* /*name*/, JobCounter& handle, std::function<void()> func)
{
    handle.m_counter.fetch_add(1);
    g_outstandingJobs.fetch_add(1);

    g_pool->Dispatch([&handle, func = std::move(func)]() mutable {
        func();

        handle.m_counter.fetch_sub(1);
        handle.m_counter.notify_all();

        g_outstandingJobs.fetch_sub(1);
        g_outstandingJobs.notify_all();
    });
}

void Job::Wait(JobCounter& handle, int64_t value, int64_t reset, const std::source_location)
{
    while (true)
    {
        int64_t expected = value;
        if (handle.m_counter.compare_exchange_strong(expected, reset))
        {
            return;
        }
        // `expected` now holds the observed value; block until it changes.
        handle.m_counter.wait(expected);
    }
}

void Job::Wait(JobCounter& handle, int64_t value, const std::source_location location)
{
    Job::Wait(handle, value, value, location);
}

void Job::Wait(JobCounter& handle, const std::source_location location)
{
    Job::Wait(handle, 0, 0, location);
}

void Job::YieldJob()
{
    std::this_thread::yield();
}

void JobSystem::Start(std::function<void()> mainJob)
{
    ThreadPool pool;
    g_pool = &pool;

    JobCounter handle;
    Job::Dispatch("Starting Job", handle, std::move(mainJob));

    int64_t remaining = g_outstandingJobs.load();
    while (remaining != 0)
    {
        g_outstandingJobs.wait(remaining);
        remaining = g_outstandingJobs.load();
    }

    pool.Shutdown();
    g_pool = nullptr;
}

} // namespace migi
