#include "gatekeeper/net/event_loop.h"

#include <cerrno>
#include <stdexcept>
#include <sys/epoll.h>
#include <unistd.h>
#include <vector>

namespace gatekeeper::net
{

EventLoop::EventLoop()
    : epoll_fd_(epoll_create1(EPOLL_CLOEXEC))
{
    if (epoll_fd_ == -1)
    {
        throw std::runtime_error("epoll_create1 failed");
    }
}

EventLoop::~EventLoop()
{
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
    std::vector<epoll_event> events(64);
    const int n = epoll_wait(epoll_fd_, events.data(), static_cast<int>(events.size()), timeout_ms);
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
}

void EventLoop::Stop()
{
    running_ = false;
}

}
