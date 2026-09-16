#pragma once

#include "gatekeeper/net/request_handler.h"

#include <cstdint>
#include <span>

namespace gatekeeper::net
{

class ClientConnection
{
public:
    ClientConnection(int client_fd, const RequestHandler& handler);
    ~ClientConnection();
    ClientConnection(const ClientConnection&) = delete;
    ClientConnection& operator=(const ClientConnection&) = delete;
    void Serve();

private:
    int client_fd_;
    const RequestHandler& handler_;

    bool SendAll(std::span<const std::uint8_t> bytes);
};

}
