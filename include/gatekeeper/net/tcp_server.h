#pragma once

namespace gatekeeper::net
{

class TcpServer
{
public:
    explicit TcpServer(int port);
    void Run();

private:
    int port_;
    int server_fd_;

    void SetupSocket();
    void AcceptConnection();
};

}
