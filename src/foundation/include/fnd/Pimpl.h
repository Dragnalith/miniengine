#pragma once

#include <memory>
#include <utility>

namespace migi
{

template <typename T>
class Pimpl
{
public:
    template <typename... Args>
    explicit Pimpl(Args&&... args)
        : m_content(std::make_unique<T>(std::forward<Args>(args)...))
    {
    }

    Pimpl(Pimpl&&) noexcept = default;
    Pimpl& operator=(Pimpl&&) noexcept = default;

    Pimpl(const Pimpl&) = delete;
    Pimpl& operator=(const Pimpl&) = delete;

    T* operator->()
    {
        return m_content.get();
    }

    const T* operator->() const
    {
        return m_content.get();
    }

    T* get()
    {
        return m_content.get();
    }

    const T* get() const
    {
        return m_content.get();
    }

private:
    std::unique_ptr<T> m_content;
};

}
