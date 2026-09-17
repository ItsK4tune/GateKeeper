#pragma once

#include "gatekeeper/log/logger.h"
#include "gatekeeper/net/request_handler.h"

#include <cstdint>
#include <memory>
#include <span>

namespace gatekeeper::net
{

class Connection
{
public:
    Connection(int client_fd, const RequestHandler& handler, std::shared_ptr<log::Logger> logger = log::Logger::Null());
    ~Connection();
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    void Serve();

private:
    int client_fd_;
    const RequestHandler& handler_;
    std::shared_ptr<log::Logger> logger_;

    bool SendAll(std::span<const std::uint8_t> bytes);
};

using ClientConnection = Connection;

}
