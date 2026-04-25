#pragma once

#include <condition_variable>
#include <mutex>
#include <utility>
#include <vector>

#include <fnd/Time.h>

namespace migi
{

template <typename T>
class TaskQueue
{
public:
    void Push(T&& task)
    {
        std::scoped_lock<std::mutex> scope(m_mutex);
        m_tasks.emplace_back(std::forward<T>(task));
        m_cv.notify_one();
    }

    std::vector<T> PopAll()
    {
        std::scoped_lock<std::mutex> scope(m_mutex);
        if (m_isClosed)
            return {};

        std::vector<T> result;
        m_tasks.swap(result);
        return result;
    }

    void Wait()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] { return HasWorkOrClosed(); });
    }

    bool WaitFor(TimeSpan timeout)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, timeout.ToChronoDuration(), [this] { return HasWorkOrClosed(); });
    }

    bool WaitUntil(TimePoint timeout)
    {
        TimeSpan remaining = timeout - TimePoint::Now();
        return WaitFor(remaining);
    }

    bool IsClosed() const
    {
        std::scoped_lock<std::mutex> scope(m_mutex);
        return m_isClosed;
    }

    void Close()
    {
        std::scoped_lock<std::mutex> scope(m_mutex);
        m_isClosed = true;
        m_cv.notify_one();
    }

private:
    bool HasWorkOrClosed() const
    {
        return !m_tasks.empty() || m_isClosed;
    }

    bool m_isClosed = false;
    std::vector<T> m_tasks;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
};

}
