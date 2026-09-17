#include "gatekeeper/net/server.h"
#include "gatekeeper/net/channel.h"
#include "gatekeeper/net/error.h"

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>
#include <utility>

namespace gatekeeper::net
{

namespace
{

void SetNonBlocking(int fd)
{
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags != -1)
    {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

}

Server::Server(int port, RequestHandler handler, std::shared_ptr<log::Logger> logger,
               int timer_interval_ms, TimerCallback timer_callback)
    : port_(port),
      server_fd_(-1),
      handler_(std::move(handler)),
      logger_(std::move(logger)),
      timer_interval_ms_(timer_interval_ms),
      timer_callback_(std::move(timer_callback))
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
    Stop();
    if (server_fd_ != -1)
    {
        logger_->Info("Closing server listening socket fd=" + std::to_string(server_fd_));
        close(server_fd_);
    }
}

void Server::SetTimerCallback(int interval_ms, TimerCallback timer_callback)
{
    timer_interval_ms_ = interval_ms;
    timer_callback_ = std::move(timer_callback);
    if (loop_)
    {
        loop_->SetPeriodicTimer(timer_interval_ms_, timer_callback_);
    }
}

void Server::Stop()
{
    if (loop_)
    {
        loop_->Stop();
    }
}

void Server::Run()
{
    SetupSocket();
    loop_ = std::make_unique<EventLoop>();
    if (timer_interval_ms_ >= 0 && timer_callback_)
    {
        loop_->SetPeriodicTimer(timer_interval_ms_, timer_callback_);
    }
    std::unordered_map<int, std::unique_ptr<Channel>> channels;

    loop_->Add(server_fd_, EPOLLIN, [this, &channels](std::uint32_t) {
        while (true)
        {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            const int client_fd = accept(server_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
            if (client_fd == -1)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    break;
                }
                if (errno == EINTR)
                {
                    continue;
                }
                break;
            }
            SetNonBlocking(client_fd);
            char ip_str[INET_ADDRSTRLEN] = "unknown";
            inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
            const auto client_info = std::string(ip_str) + ":" + std::to_string(ntohs(client_addr.sin_port));
            const auto session_id = next_session_id_++;
            logger_->Info("Accepted connection from " + client_info + " (fd=" + std::to_string(client_fd) + ")");

            auto channel = std::make_unique<Channel>(client_fd, *loop_, handler_, logger_, session_id, client_info);
            channels.emplace(client_fd, std::move(channel));

            loop_->Add(client_fd, EPOLLIN | EPOLLRDHUP | EPOLLERR, [&channels, client_fd](std::uint32_t ev) {
                auto it = channels.find(client_fd);
                if (it != channels.end())
                {
                    it->second->HandleEvents(ev);
                    if (it->second->IsClosed())
                    {
                        channels.erase(it);
                    }
                }
            });
        }
    });

    loop_->Run();
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
    if (listen(server_fd_, 128) == -1)
    {
        const auto error_str = DescribeError("listen on", endpoint, errno);
        logger_->Error(error_str);
        throw std::runtime_error(error_str);
    }
    SetNonBlocking(server_fd_);
    logger_->Info("GateKeeper server listening on " + endpoint);
}

}
