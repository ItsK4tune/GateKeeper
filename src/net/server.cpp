#include "gatekeeper/net/server.h"
#include "gatekeeper/net/channel.h"
#include "gatekeeper/net/error.h"
#include "gatekeeper/protocol/http/http_channel.h"

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
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

void ConfigureClientSocket(int fd)
{
    SetNonBlocking(fd);
    int enable_nodelay = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enable_nodelay, sizeof(enable_nodelay));
    int buf_size = 128 * 1024;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size));
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size));
}

}

Server::Server(int port, RequestHandler handler, std::shared_ptr<log::Logger> logger,
               int timer_interval_ms, TimerCallback timer_callback,
               int http_port, HttpHandler http_handler,
               std::size_t num_workers)
    : port_(port),
      handler_(std::move(handler)),
      logger_(std::move(logger)),
      timer_interval_ms_(timer_interval_ms),
      timer_callback_(std::move(timer_callback)),
      http_port_(http_port),
      http_handler_(std::move(http_handler)),
      num_workers_(num_workers)
{
    if (!logger_)
    {
        logger_ = log::Logger::Null();
    }
    if (port < 1 || port > 65535)
    {
        throw std::invalid_argument("INVALID_PORT: port must be between 1 and 65535");
    }
    if (http_port_ != 0 && (http_port_ < 1 || http_port_ > 65535))
    {
        throw std::invalid_argument("INVALID_PORT: http port must be between 1 and 65535");
    }
    if (http_port_ > 0 && http_port_ == port_)
    {
        throw std::invalid_argument("INVALID_PORT: http port cannot be the same as gatekeeper tcp port");
    }
    if (!handler_)
    {
        throw std::invalid_argument("request handler is required");
    }
    logger_->Info("Initializing GateKeeper server on port " + std::to_string(port_));
    if (http_port_ > 0)
    {
        logger_->Info("HTTP API enabled on port " + std::to_string(http_port_));
    }
}

Server::~Server()
{
    Stop();
}

void Server::SetTimerCallback(int interval_ms, TimerCallback timer_callback)
{
    timer_interval_ms_ = interval_ms;
    timer_callback_ = std::move(timer_callback);
    if (!loops_.empty() && loops_[0])
    {
        loops_[0]->SetPeriodicTimer(timer_interval_ms_, timer_callback_);
    }
}

void Server::SetHttpHandler(int http_port, HttpHandler http_handler)
{
    http_port_ = http_port;
    http_handler_ = std::move(http_handler);
}

void Server::Stop()
{
    for (auto& l : loops_)
    {
        if (l)
        {
            l->Stop();
        }
    }
}

int Server::CreateListeningSocket(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1)
    {
        throw std::runtime_error("socket creation failed: " + DescribeError("socket", "0.0.0.0:" + std::to_string(port), errno));
    }
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1)
    {
        const auto err = errno;
        close(fd);
        throw std::runtime_error(DescribeError("bind", "0.0.0.0:" + std::to_string(port), err));
    }
    if (listen(fd, 65535) == -1)
    {
        const auto err = errno;
        close(fd);
        throw std::runtime_error(DescribeError("listen", "0.0.0.0:" + std::to_string(port), err));
    }
    SetNonBlocking(fd);
    return fd;
}

void Server::RunWorkerLoop(int worker_id, EventLoop& loop, int server_fd, int http_fd)
{
    if (worker_id == 0 && timer_interval_ms_ >= 0 && timer_callback_)
    {
        loop.SetPeriodicTimer(timer_interval_ms_, timer_callback_);
    }

    std::unordered_map<int, std::unique_ptr<Channel>> channels;
    std::unordered_map<int, std::unique_ptr<HttpChannel>> http_channels;

    loop.Add(server_fd, EPOLLIN, [this, &loop, &channels, server_fd, worker_id](std::uint32_t) {
        while (true)
        {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            const int client_fd = accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
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
            ConfigureClientSocket(client_fd);
            char ip_str[INET_ADDRSTRLEN] = "unknown";
            inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
            const auto client_info = std::string(ip_str) + ":" + std::to_string(ntohs(client_addr.sin_port));
            const auto session_id = next_session_id_++;
            logger_->Info("[Worker " + std::to_string(worker_id) + "] Accepted connection from " + client_info + " (fd=" + std::to_string(client_fd) + ")");

            auto channel = std::make_unique<Channel>(client_fd, loop, handler_, logger_, session_id, client_info);
            channels.emplace(client_fd, std::move(channel));

            loop.Add(client_fd, EPOLLIN | EPOLLRDHUP | EPOLLERR, [&channels, client_fd](std::uint32_t ev) {
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

    if (http_fd != -1)
    {
        loop.Add(http_fd, EPOLLIN, [this, &loop, &http_channels, http_fd, worker_id](std::uint32_t) {
            while (true)
            {
                sockaddr_in client_addr{};
                socklen_t client_len = sizeof(client_addr);
                const int client_fd = accept(http_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
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
                ConfigureClientSocket(client_fd);
                char ip_str[INET_ADDRSTRLEN] = "unknown";
                inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
                const auto client_info = std::string(ip_str) + ":" + std::to_string(ntohs(client_addr.sin_port));
                logger_->Info("[Worker " + std::to_string(worker_id) + "] Accepted HTTP connection from " + client_info + " (fd=" + std::to_string(client_fd) + ")");

                auto channel = std::make_unique<HttpChannel>(client_fd, loop, http_handler_, logger_, client_info);
                http_channels.emplace(client_fd, std::move(channel));

                loop.Add(client_fd, EPOLLIN | EPOLLRDHUP | EPOLLERR, [&http_channels, client_fd](std::uint32_t ev) {
                    auto it = http_channels.find(client_fd);
                    if (it != http_channels.end())
                    {
                        it->second->HandleEvents(ev);
                        if (it->second->IsClosed())
                        {
                            http_channels.erase(it);
                        }
                    }
                });
            }
        });
    }

    loop.Run();

    close(server_fd);
    if (http_fd != -1)
    {
        close(http_fd);
    }
}

void Server::Run()
{
    // Bind main sockets first in the calling thread so bind errors are reported immediately
    int main_server_fd = CreateListeningSocket(port_);
    int main_http_fd = (http_port_ > 0 && http_handler_) ? CreateListeningSocket(http_port_) : -1;

    const unsigned int hw_threads = std::thread::hardware_concurrency();
    const std::size_t worker_count = num_workers_ > 0 ? num_workers_ : std::max(1U, hw_threads);

    logger_->Info("Starting GateKeeper server with " + std::to_string(worker_count) + " workers (SO_REUSEPORT)");

    loops_.resize(worker_count);
    for (std::size_t i = 0; i < worker_count; ++i)
    {
        loops_[i] = std::make_unique<EventLoop>();
    }

    std::vector<std::thread> threads;
    for (std::size_t i = 1; i < worker_count; ++i)
    {
        threads.emplace_back([this, i]() {
            try
            {
                int s_fd = CreateListeningSocket(port_);
                int h_fd = (http_port_ > 0 && http_handler_) ? CreateListeningSocket(http_port_) : -1;
                RunWorkerLoop(static_cast<int>(i), *loops_[i], s_fd, h_fd);
            }
            catch (const std::exception& e)
            {
                logger_->Error("Worker " + std::to_string(i) + " exception: " + e.what());
            }
        });
    }

    // Run worker 0 on the calling thread
    RunWorkerLoop(0, *loops_[0], main_server_fd, main_http_fd);

    for (auto& t : threads)
    {
        if (t.joinable())
        {
            t.join();
        }
    }
}

}
