#include "gatekeeper/net/tcp_server.h"

#include "gatekeeper/net/client_connection.h"
#include "gatekeeper/net/network_error.h"

#include <arpa/inet.h>
#include <iostream>
#include <cerrno>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace gatekeeper::net
{

TcpServer::TcpServer(int port, RequestHandler handler)
    : port_(port), server_fd_(-1), handler_(std::move(handler))
{
    if (port < 1 || port > 65535)
    {
        throw std::invalid_argument("INVALID_PORT: port must be between 1 and 65535");
    }
    if (!handler_)
    {
        throw std::invalid_argument("request handler is required");
    }
}

TcpServer::~TcpServer()
{
    if (server_fd_ != -1)
    {
        close(server_fd_);
    }
}

void TcpServer::Run()
{
    SetupSocket();
    while (true)
    {
        AcceptConnection();
    }
}

void TcpServer::SetupSocket()
{
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    const auto endpoint = "0.0.0.0:" + std::to_string(port_);
    if (server_fd_ == -1)
    {
        throw std::runtime_error(DescribeNetworkError("create listening socket for", endpoint, errno));
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_);
    if (bind(server_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == -1)
    {
        throw std::runtime_error(DescribeNetworkError("bind", endpoint, errno));
    }
    if (listen(server_fd_, 16) == -1)
    {
        throw std::runtime_error(DescribeNetworkError("listen on", endpoint, errno));
    }
    std::cout << "GateKeeper server listening on port " << port_ << '\n';
}

void TcpServer::AcceptConnection()
{
    const int client_fd = accept(server_fd_, nullptr, nullptr);
    if (client_fd == -1)
    {
        if (errno == EINTR)
        {
            return;
        }
        throw std::runtime_error(DescribeNetworkError("accept on", "0.0.0.0:" + std::to_string(port_), errno));
    }
    try
    {
        ClientConnection(client_fd, handler_).Serve();
    }
    catch (const std::exception& error)
    {
        std::cerr << "CLIENT_ERROR: " << error.what() << '\n';
    }
}

}
