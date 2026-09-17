#pragma once

#include "gatekeeper/net/request_handler.h"

namespace gatekeeper::net
{

class Server
{
public:
    Server(int port, RequestHandler handler);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    void Run();

private:
    int port_;
    int server_fd_;
    RequestHandler handler_;

    void SetupSocket();
    void AcceptConnection();
};

using TcpServer = Server;

}
