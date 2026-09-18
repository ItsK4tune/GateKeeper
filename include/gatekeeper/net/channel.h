#pragma once

#include "gatekeeper/core/log/logger.h"
#include "gatekeeper/net/event_loop.h"
#include "gatekeeper/net/request_handler.h"
#include "gatekeeper/net/session.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace gatekeeper::net
{

class Channel
{
public:
    Channel(int fd, EventLoop& loop, RequestHandler handler, std::shared_ptr<log::Logger> logger = log::Logger::Null(), std::uint64_t session_id = 0, std::string address = "");
    ~Channel();

    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    void HandleEvents(std::uint32_t events);
    void HandleRead();
    void HandleWrite();
    void Send(std::span<const std::uint8_t> data);
    void Close();

    [[nodiscard]] bool IsClosed() const noexcept { return closed_; }
    [[nodiscard]] int Fd() const noexcept { return fd_; }
    [[nodiscard]] const Session& GetSession() const noexcept { return session_; }

private:
    int fd_;
    EventLoop& loop_;
    RequestHandler handler_;
    std::shared_ptr<log::Logger> logger_;
    Session session_;
    std::vector<std::uint8_t> write_buffer_;
    bool closed_{false};
};

}
