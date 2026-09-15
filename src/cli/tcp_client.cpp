#include "gatekeeper/cli/tcp_client.h"

#include "gatekeeper/protocol/frame.h"

#include <array>
#include <cerrno>
#include <netdb.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace gatekeeper::cli
{

TcpClient::TcpClient(std::string host, std::uint16_t port) : host_(std::move(host)), port_(port) {}

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
        throw std::runtime_error(std::string("cannot resolve host: ") + gai_strerror(status));
    }
    for (auto* address = addresses; address != nullptr; address = address->ai_next)
    {
        socket_fd_ = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (socket_fd_ != -1 && connect(socket_fd_, address->ai_addr, address->ai_addrlen) == 0)
        {
            break;
        }
        Close();
    }
    freeaddrinfo(addresses);
    if (socket_fd_ == -1)
    {
        throw std::runtime_error("cannot connect to " + host_ + ':' + service);
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
        if (received <= 0)
        {
            throw std::runtime_error("connection read failed");
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
            throw std::runtime_error("connection write failed");
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
            throw std::runtime_error("invalid response frame size");
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
