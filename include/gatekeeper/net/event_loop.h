#pragma once

#include <cstdint>
#include <functional>
#include <unordered_map>

namespace gatekeeper::net
{

class EventLoop
{
public:
    using EventCallback = std::function<void(std::uint32_t)>;

    EventLoop();
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    bool Add(int fd, std::uint32_t events, EventCallback callback);
    bool Modify(int fd, std::uint32_t events);
    bool Remove(int fd);

    void Run();
    void RunOnce(int timeout_ms = -1);
    void Stop();

    [[nodiscard]] bool IsRunning() const noexcept { return running_; }

private:
    int epoll_fd_{-1};
    bool running_{false};
    std::unordered_map<int, EventCallback> callbacks_;
};

}
