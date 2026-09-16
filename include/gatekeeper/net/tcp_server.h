#pragma once

#include "gatekeeper/net/request_handler.h"

namespace gatekeeper::net
{

class TcpServer
{
public:
    TcpServer(int port, RequestHandler handler);
    ~TcpServer();
    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;
    void Run();

private:
    int port_;
    int server_fd_;
    RequestHandler handler_;

    void SetupSocket();
    void AcceptConnection();
};

}
