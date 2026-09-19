#include "gatekeeper/net/client.h"
#include "gatekeeper/net/error.h"
#include "gatekeeper/protocol/gkwp2/header.h"
#include "gatekeeper/protocol/gkwp2/frame.h"

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

namespace gatekeeper::net
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

Client::Client(std::string host, std::uint16_t port, std::shared_ptr<log::Logger> logger)
    : host_(std::move(host)), port_(port), logger_(std::move(logger))
{
    if (!logger_)
    {
        logger_ = log::Logger::Null();
    }
    if (host_.empty())
    {
        throw std::invalid_argument("INVALID_HOST: host cannot be empty");
    }
    if (port_ == 0)
    {
        throw std::invalid_argument("INVALID_PORT: port must be between 1 and 65535");
    }
}

Client::~Client()
{
    Close();
}

void Client::Close()
{
    if (socket_fd_ != -1)
    {
        logger_->Info("Closing connection to " + host_ + ':' + std::to_string(port_));
        close(socket_fd_);
        socket_fd_ = -1;
    }
}

void Client::Open()
{
    if (socket_fd_ != -1)
    {
        return;
    }
    const auto service = std::to_string(port_);
    logger_->Info("Connecting to " + host_ + ':' + service + "...");
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    addrinfo* addresses = nullptr;
    const int status = getaddrinfo(host_.c_str(), service.c_str(), &hints, &addresses);
    if (status != 0)
    {
        const auto err_msg = "DNS_ERROR: cannot resolve " + host_ + ':' + service + ": " +
                             gai_strerror(status) + ". Check the host name or use an IP address.";
        logger_->Error(err_msg);
        throw std::runtime_error(err_msg);
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
        const auto err_msg = DescribeError("connect to", host_ + ':' + service, last_error);
        logger_->Error(err_msg);
        throw std::runtime_error(err_msg);
    }
    const timeval timeout{5, 0};
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1 ||
        setsockopt(socket_fd_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == -1)
    {
        const int error = errno;
        Close();
        const auto err_msg = DescribeError("set timeout for", host_ + ':' + service, error);
        logger_->Error(err_msg);
        throw std::runtime_error(err_msg);
    }
    logger_->Info("Connected to " + host_ + ':' + service);
}

void Client::ReadAll(std::span<std::uint8_t> bytes)
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
            const auto err_msg = "CONNECTION_CLOSED: " + host_ + ':' + std::to_string(port_) +
                                 " closed before the complete response was received.";
            logger_->Error(err_msg);
            throw std::runtime_error(err_msg);
        }
        if (received < 0)
        {
            const auto err_msg = DescribeError("read from", host_ + ':' + std::to_string(port_), errno);
            logger_->Error(err_msg);
            throw std::runtime_error(err_msg);
        }
        bytes = bytes.subspan(static_cast<std::size_t>(received));
    }
}

void Client::SendAll(std::span<const std::uint8_t> bytes)
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
            const auto err_msg = DescribeError("write to", host_ + ':' + std::to_string(port_),
                                               written == 0 ? EPIPE : errno);
            logger_->Error(err_msg);
            throw std::runtime_error(err_msg);
        }
        bytes = bytes.subspan(static_cast<std::size_t>(written));
    }
}

std::string Client::Execute(const std::string& operation, const std::string& body)
{
    if (operation.empty() || operation.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._") != std::string::npos)
    {
        throw std::invalid_argument("invalid operation name");
    }
    Open();
    try
    {
        const auto req_num = next_id_++;
        const auto req_id = "gate-" + std::to_string(req_num);
        const auto payload = "{\"id\":\"" + req_id +
                             "\",\"op\":\"" + operation + "\",\"body\":" + body + "}";
        logger_->Info("Sending request id=\"" + req_id + "\" op=\"" + operation + "\"");

        protocol::gkwp2::Header hdr{};
        hdr.magic = protocol::gkwp2::kMagic;
        hdr.version = protocol::gkwp2::kVersion;
        hdr.flags = protocol::gkwp2::flags::kEndStream;
        hdr.msg_type = static_cast<std::uint8_t>(protocol::gkwp2::MsgType::Request);
        hdr.request_id = req_num;
        hdr.stream_id = 1;
        hdr.payload_len = static_cast<std::uint32_t>(payload.size());

        SendAll(protocol::gkwp2::EncodeFrame(hdr, payload));
        logger_->Debug("Sent GKWP/2 request frame for id=\"" + req_id + "\"");

        std::array<std::uint8_t, protocol::gkwp2::kHeaderSize> header_buf{};
        ReadAll(header_buf);

        const auto magic = (static_cast<std::uint16_t>(header_buf[0]) << 8U) | header_buf[1];
        if (magic != protocol::gkwp2::kMagic)
        {
            const auto err_msg = "INVALID_MAGIC: expected GKWP/2 magic from " + host_ + ':' + std::to_string(port_);
            logger_->Error(err_msg);
            throw std::runtime_error(err_msg);
        }

        const auto length = (static_cast<std::uint32_t>(header_buf[20]) << 24U) |
                            (static_cast<std::uint32_t>(header_buf[21]) << 16U) |
                            (static_cast<std::uint32_t>(header_buf[22]) << 8U) | header_buf[23];

        if (length > protocol::gkwp2::kMaxPayloadLength)
        {
            const auto err_msg = "INVALID_FRAME: response from " + host_ + ':' + std::to_string(port_) +
                                 " declares " + std::to_string(length) + " bytes; exceeds limit.";
            logger_->Error(err_msg);
            throw std::runtime_error(err_msg);
        }

        std::vector<std::uint8_t> response_bytes(length);
        if (length > 0)
        {
            ReadAll(response_bytes);
        }
        std::string response(response_bytes.begin(), response_bytes.end());
        logger_->Info("Received GKWP/2 response: " + response);
        return response;
    }
    catch (...)
    {
        Close();
        throw;
    }
}

}
