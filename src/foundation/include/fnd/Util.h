#pragma once

#include <assert.h>
#include <memory>
#include <condition_variable>
#include <mutex>
#include <functional>
#include <chrono>
namespace engine
{

using Clock = std::chrono::high_resolution_clock;
using Time = decltype(Clock::now());
using Duration = decltype(Time() - Time());

inline int to_us(Duration val) {
    return (int) std::chrono::duration_cast<std::chrono::microseconds>(val).count();
}


#if 1
#define ASSERT_MSG(condition, msg) assert(msg && condition)
#else
#define ASSERT_MSG(condition, msg) void()
#endif

template <typename T>
class Pimpl
{
public:
    template <typename... Args>
    Pimpl(Args&&... args) : m_content(std::make_unique<T>(std::forward<Args>(args)...))
    {
    }

    Pimpl(Pimpl&& other)
        : m_content(std::move(other.m_impl))
    {}

    T* operator ->() {
        return m_content.get();
    }

    const T* operator->() const{
        return m_content.get();
    }

    T* get() {
        return m_content.get();
    }

    const T* get() const {
        return m_content.get();
    }

private:
    std::unique_ptr<T> m_content;
};

template <typename T>
class TaskQueue {
public:
    void Push(T&& t) 
    {
        std::scoped_lock<std::mutex> scope(m_mutex);
        m_tasks.emplace_back(std::forward<T>(t));
        m_cv.notify_one();
    }

    std::vector<T> PopAll() {
        std::scoped_lock<std::mutex> scope(m_mutex);
        if (!m_isClosed) {
            std::vector<T> result;
            m_tasks.swap(result);
            return result;
        }

        return std::vector<T>();
    }

    void Wait() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, m_condition);
    }

    template< class Rep, class Period >
    bool WaitFor(const std::chrono::duration<Rep, Period>& rel_time) {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, rel_time, m_condition());
    }

    template<class Clock, class Duration>
    bool WaitUntil(const std::chrono::time_point<Clock, Duration>& timeout_time) {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_until(lock, timeout_time, m_condition);
    }

    bool IsClosed() const {
        std::scoped_lock<std::mutex> scope(m_mutex);
        return m_isClosed;
    }

    void Close() {
        std::scoped_lock<std::mutex> scope(m_mutex);
        m_isClosed = true;
        m_cv.notify_one();
    }

private:
    bool m_isClosed = false;
    std::vector<T> m_tasks;
    mutable std::mutex m_mutex;
    mutable std::condition_variable m_cv;
    std::function<bool()> m_condition = [this] {return m_tasks.size() > 0 || m_isClosed; };
};

void RandomWorkload(int microsecond, int random_percent = 0);

}