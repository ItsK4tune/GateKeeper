#pragma once

#include "gatekeeper/net/request_handler.h"

#include <cstdint>
#include <span>

namespace gatekeeper::net
{

class Connection
{
public:
    Connection(int client_fd, const RequestHandler& handler);
    ~Connection();
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    void Serve();

private:
    int client_fd_;
    const RequestHandler& handler_;

    bool SendAll(std::span<const std::uint8_t> bytes);
};

using ClientConnection = Connection;

}
