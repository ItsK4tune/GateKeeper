#include "gatekeeper/cli/tcp_client.h"

#include "gatekeeper/protocol/frame.h"
#include "gatekeeper/net/network_error.h"

#include <array>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace gatekeeper::cli
{
namespace
{

int ConnectWithTimeout(int socket_fd, const sockaddr* address, socklen_t length)
{
    const int flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags == -1 || fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        return errno;
    }
    int error = 0;
    if (connect(socket_fd, address, length) == -1)
    {
        error = errno;
        if (error == EINPROGRESS)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            pollfd descriptor{socket_fd, POLLOUT, 0};
            while (true)
            {
                const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now()).count();
                if (remaining <= 0)
                {
                    error = ETIMEDOUT;
                    break;
                }
                const int ready = poll(&descriptor, 1, static_cast<int>(remaining));
                if (ready == -1 && errno == EINTR)
                {
                    continue;
                }
                if (ready <= 0)
                {
                    error = ready == 0 ? ETIMEDOUT : errno;
                    break;
                }
                socklen_t size = sizeof(error);
                if (getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &error, &size) == -1)
                {
                    error = errno;
                }
                break;
            }
        }
    }
    if (fcntl(socket_fd, F_SETFL, flags) == -1 && error == 0)
    {
        error = errno;
    }
    return error;
}

}

TcpClient::TcpClient(std::string host, std::uint16_t port) : host_(std::move(host)), port_(port)
{
    if (host_.empty())
    {
        throw std::invalid_argument("INVALID_HOST: host cannot be empty");
    }
    if (port_ == 0)
    {
        throw std::invalid_argument("INVALID_PORT: port must be between 1 and 65535");
    }
}

TcpClient::~TcpClient()
{
    Close();
}

void TcpClient::Close()
{
    if (socket_fd_ != -1)
    {
        close(socket_fd_);
        socket_fd_ = -1;
    }
}

void TcpClient::Connect()
{
    if (socket_fd_ != -1)
    {
        return;
    }
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    addrinfo* addresses = nullptr;
    const auto service = std::to_string(port_);
    const int status = getaddrinfo(host_.c_str(), service.c_str(), &hints, &addresses);
    if (status != 0)
    {
        throw std::runtime_error("DNS_ERROR: cannot resolve " + host_ + ':' + service + ": " +
                                 gai_strerror(status) + ". Check the host name or use an IP address.");
    }
    int last_error = ECONNREFUSED;
    for (auto* address = addresses; address != nullptr; address = address->ai_next)
    {
        socket_fd_ = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (socket_fd_ == -1)
        {
            last_error = errno;
            continue;
        }
        last_error = ConnectWithTimeout(socket_fd_, address->ai_addr, address->ai_addrlen);
        if (last_error == 0)
        {
            break;
        }
        Close();
    }
    freeaddrinfo(addresses);
    if (socket_fd_ == -1)
    {
        throw std::runtime_error(net::DescribeNetworkError("connect to", host_ + ':' + service, last_error));
    }
    const timeval timeout{5, 0};
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1 ||
        setsockopt(socket_fd_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == -1)
    {
        const int error = errno;
        Close();
        throw std::runtime_error(net::DescribeNetworkError("set timeout for", host_ + ':' + service, error));
    }
}

void TcpClient::ReadAll(std::span<std::uint8_t> bytes)
{
    while (!bytes.empty())
    {
        const auto received = recv(socket_fd_, bytes.data(), bytes.size(), 0);
        if (received == -1 && errno == EINTR)
        {
            continue;
        }
        if (received == 0)
        {
            throw std::runtime_error("CONNECTION_CLOSED: " + host_ + ':' + std::to_string(port_) +
                                     " closed before the complete response was received.");
        }
        if (received < 0)
        {
            throw std::runtime_error(net::DescribeNetworkError("read from", host_ + ':' + std::to_string(port_), errno));
        }
        bytes = bytes.subspan(static_cast<std::size_t>(received));
    }
}

void TcpClient::SendAll(std::span<const std::uint8_t> bytes)
{
    while (!bytes.empty())
    {
        const auto written = send(socket_fd_, bytes.data(), bytes.size(), MSG_NOSIGNAL);
        if (written == -1 && errno == EINTR)
        {
            continue;
        }
        if (written <= 0)
        {
            throw std::runtime_error(net::DescribeNetworkError("write to", host_ + ':' + std::to_string(port_),
                                                               written == 0 ? EPIPE : errno));
        }
        bytes = bytes.subspan(static_cast<std::size_t>(written));
    }
}

std::string TcpClient::Execute(const std::string& operation)
{
    if (operation.empty() || operation.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._") != std::string::npos)
    {
        throw std::invalid_argument("invalid operation name");
    }
    Connect();
    try
    {
        const auto payload = "{\"id\":\"gate-" + std::to_string(next_id_++) +
                             "\",\"op\":\"" + operation + "\",\"body\":{}}";
        SendAll(protocol::EncodeFrame(payload));
        std::array<std::uint8_t, 4> header{};
        ReadAll(header);
        const auto length = (static_cast<std::uint32_t>(header[0]) << 24U) |
                            (static_cast<std::uint32_t>(header[1]) << 16U) |
                            (static_cast<std::uint32_t>(header[2]) << 8U) | header[3];
        if (length == 0 || length > protocol::kMaxFrameSize)
        {
            throw std::runtime_error("INVALID_FRAME: response from " + host_ + ':' + std::to_string(port_) +
                                     " declares " + std::to_string(length) + " bytes; expected 1..1048576.");
        }
        std::vector<std::uint8_t> response(length);
        ReadAll(response);
        return {response.begin(), response.end()};
    }
    catch (...)
    {
        Close();
        throw;
    }
}

}
