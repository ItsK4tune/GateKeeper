#pragma once

#include "gatekeeper/log/logger.h"
#include "gatekeeper/net/event_loop.h"
#include "gatekeeper/net/request_handler.h"

#include <cstdint>
#include <functional>
#include <memory>

namespace gatekeeper::net
{

class Server
{
public:
    using TickCallback = std::function<void()>;

    Server(int port, RequestHandler handler, std::shared_ptr<log::Logger> logger = log::Logger::Null(),
           int timer_interval_ms = -1, TickCallback tick_handler = nullptr);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    void SetTickHandler(int interval_ms, TickCallback tick_handler);
    [[nodiscard]] int GetTimerInterval() const noexcept { return timer_interval_ms_; }

    void Run();
    void Stop();

private:
    int port_;
    int server_fd_;
    RequestHandler handler_;
    std::shared_ptr<log::Logger> logger_;
    int timer_interval_ms_{-1};
    TickCallback tick_handler_;
    std::uint64_t next_session_id_{1};
    std::unique_ptr<EventLoop> loop_;

    void SetupSocket();
};

using TcpServer = Server;

}
