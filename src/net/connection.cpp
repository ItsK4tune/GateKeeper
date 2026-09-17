#include "gatekeeper/net/connection.h"
#include "gatekeeper/net/error.h"
#include "gatekeeper/protocol/decoder.h"
#include "gatekeeper/protocol/frame.h"
#include "gatekeeper/protocol/response.h"

#include <array>
#include <cerrno>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>

namespace gatekeeper::net
{

Connection::Connection(int client_fd, const RequestHandler& handler)
    : client_fd_(client_fd), handler_(handler) {}

Connection::~Connection()
{
    close(client_fd_);
}

bool Connection::SendAll(std::span<const std::uint8_t> bytes)
{
    while (!bytes.empty())
    {
        const auto written = send(client_fd_, bytes.data(), bytes.size(), MSG_NOSIGNAL);
        if (written > 0)
        {
            bytes = bytes.subspan(static_cast<std::size_t>(written));
            continue;
        }
        if (written == -1 && errno == EINTR)
        {
            continue;
        }
        const int error = written == 0 ? EPIPE : errno;
        std::cerr << DescribeError("send to", "client fd=" + std::to_string(client_fd_), error) << '\n';
        return false;
    }
    return true;
}

void Connection::Serve()
{
    protocol::Decoder decoder;
    std::array<std::uint8_t, 4096> buffer{};
    while (true)
    {
        const auto received = recv(client_fd_, buffer.data(), buffer.size(), 0);
        if (received == 0)
        {
            return;
        }
        if (received < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            const int error = errno;
            std::cerr << DescribeError("read from", "client fd=" + std::to_string(client_fd_), error) << '\n';
            return;
        }
        std::vector<std::string> payloads;
        protocol::ProtocolError error;
        if (!decoder.Push(std::span(buffer.data(), static_cast<std::size_t>(received)), payloads, error))
        {
            SendAll(protocol::EncodeFrame(protocol::EncodeErrorResponse("", error)));
            return;
        }
        for (const auto& payload : payloads)
        {
            if (!SendAll(protocol::EncodeFrame(handler_(payload))))
            {
                return;
            }
        }
    }
}

}
