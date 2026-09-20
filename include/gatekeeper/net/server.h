#pragma once

#include "gatekeeper/core/log/logger.h"
#include "gatekeeper/net/event_loop.h"
#include "gatekeeper/protocol/http/http_channel.h"
#include "gatekeeper/net/request_handler.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace gatekeeper::net
{

class Server
{
public:
    using TimerCallback = EventLoop::TimerCallback;

    Server(int port, RequestHandler handler, std::shared_ptr<log::Logger> logger = log::Logger::Null(),
           int timer_interval_ms = -1, TimerCallback timer_callback = nullptr,
           int http_port = 0, HttpHandler http_handler = nullptr,
           std::size_t num_workers = 0);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    void SetTimerCallback(int interval_ms, TimerCallback timer_callback);
    [[nodiscard]] int GetTimerInterval() const noexcept { return timer_interval_ms_; }
    void SetHttpHandler(int http_port, HttpHandler http_handler);

    void Run();
    void Stop();

private:
    void RunWorkerLoop(int worker_id, EventLoop& loop, int server_fd, int http_fd);
    int CreateListeningSocket(int port);

    int port_;
    RequestHandler handler_;
    std::shared_ptr<log::Logger> logger_;
    int timer_interval_ms_{-1};
    TimerCallback timer_callback_{nullptr};
    int http_port_{0};
    HttpHandler http_handler_{nullptr};
    std::size_t num_workers_{0};
    std::vector<std::unique_ptr<EventLoop>> loops_;
    std::atomic<std::uint64_t> next_session_id_{1};
};

}
