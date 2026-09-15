#include "gatekeeper/net/tcp_server.h"

#include "gatekeeper/net/client_connection.h"

#include <arpa/inet.h>
#include <iostream>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace gatekeeper::net
{

TcpServer::TcpServer(int port) : port_(port), server_fd_(-1) {}

void TcpServer::Run()
{
    SetupSocket();
    while (true) AcceptConnection();
}

void TcpServer::SetupSocket()
{
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ == -1) throw std::runtime_error("Failed to create socket");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_);
    if (bind(server_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == -1) throw std::runtime_error("Failed to bind socket");
    if (listen(server_fd_, 16) == -1) throw std::runtime_error("Failed to listen");
    std::cout << "GateKeeper server listening on port " << port_ << '\n';
}

void TcpServer::AcceptConnection()
{
    const int client_fd = accept(server_fd_, nullptr, nullptr);
    if (client_fd == -1) throw std::runtime_error("Failed to accept client");
    ClientConnection(client_fd).Serve();
}

}
