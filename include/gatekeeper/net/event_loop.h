#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <unordered_map>

namespace gatekeeper::net
{

class EventLoop
{
public:
    using EventCallback = std::function<void(std::uint32_t)>;
    using TimerCallback = std::function<void()>;

    EventLoop();
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    bool Add(int fd, std::uint32_t events, EventCallback callback);
    bool Modify(int fd, std::uint32_t events);
    bool Remove(int fd);

    void SetPeriodicTimer(int interval_ms, TimerCallback callback);
    void SetTimer(int interval_ms, TimerCallback callback);

    [[nodiscard]] int GetTimerInterval() const noexcept { return timer_interval_ms_; }
    [[nodiscard]] bool HasTimer() const noexcept { return timer_interval_ms_ >= 0 && timer_callback_ != nullptr; }

    void Run();
    void RunOnce(int timeout_ms = -1);
    void Stop();

    [[nodiscard]] bool IsRunning() const noexcept { return running_; }

private:
    int epoll_fd_{-1};
    bool running_{false};
    int timer_interval_ms_{-1};
    TimerCallback timer_callback_;
    std::chrono::steady_clock::time_point last_tick_{};
    std::unordered_map<int, EventCallback> callbacks_;
};

}
