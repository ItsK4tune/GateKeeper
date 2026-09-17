#include "gatekeeper/net/server.h"
#include "gatekeeper/net/connection.h"
#include "gatekeeper/net/error.h"

#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace gatekeeper::net
{

Server::Server(int port, RequestHandler handler, std::shared_ptr<log::Logger> logger)
    : port_(port), server_fd_(-1), handler_(std::move(handler)), logger_(std::move(logger))
{
    if (!logger_)
    {
        logger_ = log::Logger::Null();
    }
    if (port < 1 || port > 65535)
    {
        throw std::invalid_argument("INVALID_PORT: port must be between 1 and 65535");
    }
    if (!handler_)
    {
        throw std::invalid_argument("request handler is required");
    }
    logger_->Info("Initializing GateKeeper server on port " + std::to_string(port_));
}

Server::~Server()
{
    if (server_fd_ != -1)
    {
        logger_->Info("Closing server listening socket fd=" + std::to_string(server_fd_));
        close(server_fd_);
    }
}

void Server::Run()
{
    SetupSocket();
    while (true)
    {
        AcceptConnection();
    }
}

void Server::SetupSocket()
{
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    const auto endpoint = "0.0.0.0:" + std::to_string(port_);
    if (server_fd_ == -1)
    {
        const auto error_str = DescribeError("create listening socket for", endpoint, errno);
        logger_->Error(error_str);
        throw std::runtime_error(error_str);
    }
    int reuse = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_);
    if (bind(server_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == -1)
    {
        const auto error_str = DescribeError("bind", endpoint, errno);
        logger_->Error(error_str);
        throw std::runtime_error(error_str);
    }
    if (listen(server_fd_, 16) == -1)
    {
        const auto error_str = DescribeError("listen on", endpoint, errno);
        logger_->Error(error_str);
        throw std::runtime_error(error_str);
    }
    logger_->Info("GateKeeper server listening on " + endpoint);
}

void Server::AcceptConnection()
{
    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);
    const int client_fd = accept(server_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
    if (client_fd == -1)
    {
        if (errno == EINTR)
        {
            return;
        }
        const auto error_str = DescribeError("accept on", "0.0.0.0:" + std::to_string(port_), errno);
        logger_->Error(error_str);
        throw std::runtime_error(error_str);
    }
    char ip_str[INET_ADDRSTRLEN] = "unknown";
    inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
    const auto client_info = std::string(ip_str) + ":" + std::to_string(ntohs(client_addr.sin_port));
    logger_->Info("Accepted connection from " + client_info + " (fd=" + std::to_string(client_fd) + ")");
    try
    {
        Connection(client_fd, handler_, logger_).Serve();
    }
    catch (const std::exception& error)
    {
        logger_->Error(std::string("Client connection error: ") + error.what());
    }
}

}
