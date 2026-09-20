#include "gatekeeper/net/event_loop.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace gatekeeper::net
{

EventLoop::EventLoop()
    : epoll_fd_(epoll_create1(EPOLL_CLOEXEC))
{
    if (epoll_fd_ == -1)
    {
        throw std::runtime_error("epoll_create1 failed");
    }
    wakeup_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wakeup_fd_ != -1)
    {
        Add(wakeup_fd_, EPOLLIN, [this](std::uint32_t) {
            std::uint64_t val = 0;
            const auto r = read(wakeup_fd_, &val, sizeof(val));
            (void)r;
        });
    }
}

EventLoop::~EventLoop()
{
    if (wakeup_fd_ != -1)
    {
        close(wakeup_fd_);
    }
    if (epoll_fd_ != -1)
    {
        close(epoll_fd_);
    }
}

bool EventLoop::Add(int fd, std::uint32_t events, EventCallback callback)
{
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == -1)
    {
        return false;
    }
    callbacks_[fd] = std::move(callback);
    return true;
}

bool EventLoop::Modify(int fd, std::uint32_t events)
{
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    return epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) == 0;
}

bool EventLoop::Remove(int fd)
{
    callbacks_.erase(fd);
    return epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) == 0;
}

void EventLoop::SetPeriodicTimer(int interval_ms, TimerCallback callback)
{
    if (interval_ms < 0 || !callback)
    {
        timer_interval_ms_ = -1;
        timer_callback_ = nullptr;
        return;
    }
    timer_interval_ms_ = interval_ms;
    timer_callback_ = std::move(callback);
    last_tick_ = std::chrono::steady_clock::now();
}

void EventLoop::SetTimer(int interval_ms, TimerCallback callback)
{
    SetPeriodicTimer(interval_ms, std::move(callback));
}

void EventLoop::Run()
{
    running_ = true;
    while (running_)
    {
        RunOnce(-1);
    }
}

void EventLoop::RunOnce(int timeout_ms)
{
    int effective_timeout = timeout_ms;
    if (timer_interval_ms_ >= 0 && timer_callback_)
    {
        if (timer_interval_ms_ == 0)
        {
            effective_timeout = 0;
        }
        else
        {
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_tick_).count();
            const auto remaining = static_cast<int>(timer_interval_ms_ - elapsed);
            effective_timeout = (remaining > 0) ? remaining : 0;
        }
        if (timeout_ms >= 0 && timeout_ms < effective_timeout)
        {
            effective_timeout = timeout_ms;
        }
    }

    std::array<epoll_event, 64> events{};
    const int n = epoll_wait(epoll_fd_, events.data(), static_cast<int>(events.size()), effective_timeout);
    if (n < 0)
    {
        if (errno == EINTR)
        {
            return;
        }
        throw std::runtime_error("epoll_wait failed");
    }
    for (int i = 0; i < n; ++i)
    {
        const int fd = events[i].data.fd;
        auto it = callbacks_.find(fd);
        if (it != callbacks_.end() && it->second)
        {
            auto cb = it->second;
            cb(events[i].events);
        }
    }

    if (timer_interval_ms_ >= 0 && timer_callback_)
    {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_tick_).count();
        if (elapsed >= timer_interval_ms_)
        {
            last_tick_ = now;
            timer_callback_();
        }
    }
}

void EventLoop::Wakeup()
{
    if (wakeup_fd_ != -1)
    {
        std::uint64_t one = 1;
        const auto w = write(wakeup_fd_, &one, sizeof(one));
        (void)w;
    }
}

void EventLoop::Stop()
{
    running_ = false;
    Wakeup();
}

}
