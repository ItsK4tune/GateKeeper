#pragma once

#include "gatekeeper/core/log/logger.h"
#include "gatekeeper/net/event_loop.h"
#include "gatekeeper/protocol/http/http_channel.h"
#include "gatekeeper/net/request_handler.h"

#include <cstdint>
#include <functional>
#include <memory>

namespace gatekeeper::net
{

class Server
{
public:
    using TimerCallback = EventLoop::TimerCallback;

    Server(int port, RequestHandler handler, std::shared_ptr<log::Logger> logger = log::Logger::Null(),
           int timer_interval_ms = -1, TimerCallback timer_callback = nullptr,
           int http_port = 0, HttpHandler http_handler = nullptr);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    void SetTimerCallback(int interval_ms, TimerCallback timer_callback);
    [[nodiscard]] int GetTimerInterval() const noexcept { return timer_interval_ms_; }
    void SetHttpHandler(int http_port, HttpHandler http_handler);

    void Run();
    void Stop();

private:
    int port_;
    int server_fd_;
    RequestHandler handler_;
    std::shared_ptr<log::Logger> logger_;
    int timer_interval_ms_{-1};
    TimerCallback timer_callback_;
    int http_port_{0};
    int http_server_fd_{-1};
    HttpHandler http_handler_{nullptr};
    std::uint64_t next_session_id_{1};
    std::unique_ptr<EventLoop> loop_;

    void SetupSocket();
    void SetupHttpSocket();
};

}
