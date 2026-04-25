#pragma once

#include <chrono>
#include <cstdint>

namespace migi
{

class TimeSpan
{
public:
    using Clock = std::chrono::high_resolution_clock;
    using NativeDuration = Clock::duration;

    constexpr TimeSpan() = default;
    explicit constexpr TimeSpan(NativeDuration duration)
        : m_duration(duration)
    {
    }

    static constexpr TimeSpan FromMicroseconds(int64_t microseconds)
    {
        return TimeSpan(std::chrono::microseconds(microseconds));
    }

    static constexpr TimeSpan FromMilliseconds(int64_t milliseconds)
    {
        return TimeSpan(std::chrono::milliseconds(milliseconds));
    }

    static constexpr TimeSpan FromSeconds(int64_t seconds)
    {
        return TimeSpan(std::chrono::seconds(seconds));
    }

    int64_t ToMicroseconds() const
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(m_duration).count();
    }

    double ToMilliseconds() const
    {
        return std::chrono::duration<double, std::milli>(m_duration).count();
    }

    double ToSeconds() const
    {
        return std::chrono::duration<double>(m_duration).count();
    }

    NativeDuration ToChronoDuration() const
    {
        return m_duration;
    }

private:
    NativeDuration m_duration{};
};

class TimePoint
{
public:
    using Clock = std::chrono::high_resolution_clock;
    using NativeDuration = Clock::duration;

    constexpr TimePoint() = default;
    explicit constexpr TimePoint(NativeDuration value)
        : m_value(value)
    {
    }

    static TimePoint Now()
    {
        return TimePoint(Clock::now().time_since_epoch());
    }

    NativeDuration ToChronoDurationSinceEpoch() const
    {
        return m_value;
    }

private:
    NativeDuration m_value{};

    friend TimeSpan operator-(TimePoint to, TimePoint from);
    friend TimePoint operator+(TimePoint point, TimeSpan span);
    friend TimePoint operator-(TimePoint point, TimeSpan span);
};

inline TimeSpan operator-(TimePoint to, TimePoint from)
{
    return TimeSpan(to.m_value - from.m_value);
}

inline TimePoint operator+(TimePoint point, TimeSpan span)
{
    return TimePoint(point.m_value + span.ToChronoDuration());
}

inline TimePoint operator-(TimePoint point, TimeSpan span)
{
    return TimePoint(point.m_value - span.ToChronoDuration());
}

}
