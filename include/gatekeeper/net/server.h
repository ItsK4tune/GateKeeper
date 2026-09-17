#pragma once

#include "gatekeeper/log/logger.h"
#include "gatekeeper/net/request_handler.h"

#include <memory>

namespace gatekeeper::net
{

class Server
{
public:
    Server(int port, RequestHandler handler, std::shared_ptr<log::Logger> logger = log::Logger::Null());
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    void Run();

private:
    int port_;
    int server_fd_;
    RequestHandler handler_;
    std::shared_ptr<log::Logger> logger_;

    void SetupSocket();
    void AcceptConnection();
};

using TcpServer = Server;

}
