#pragma once

#include "gatekeeper/log/logger.h"
#include "gatekeeper/net/request_handler.h"
#include "gatekeeper/net/session.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace gatekeeper::net
{

class Connection
{
public:
    Connection(int client_fd, const RequestHandler& handler, std::shared_ptr<log::Logger> logger = log::Logger::Null(), std::uint64_t session_id = 0, std::string address = "");
    ~Connection();
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    void Serve();

    [[nodiscard]] const Session& GetSession() const noexcept { return session_; }

private:
    int client_fd_;
    const RequestHandler& handler_;
    std::shared_ptr<log::Logger> logger_;
    Session session_;

    bool SendAll(std::span<const std::uint8_t> bytes);
};

}
