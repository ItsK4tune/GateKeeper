#include "gatekeeper/net/connection.h"
#include "gatekeeper/net/error.h"
#include "gatekeeper/protocol/decoder.h"
#include "gatekeeper/protocol/frame.h"
#include "gatekeeper/protocol/response.h"

#include <array>
#include <cerrno>
#include <sys/socket.h>
#include <unistd.h>

namespace gatekeeper::net
{

Connection::Connection(int client_fd, const RequestHandler& handler, std::shared_ptr<log::Logger> logger)
    : client_fd_(client_fd), handler_(handler), logger_(std::move(logger))
{
    if (!logger_)
    {
        logger_ = log::Logger::Null();
    }
}

Connection::~Connection()
{
    logger_->Info("Closing client connection fd=" + std::to_string(client_fd_));
    close(client_fd_);
}

bool Connection::SendAll(std::span<const std::uint8_t> bytes)
{
    const auto total_bytes = bytes.size();
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
        const auto error_str = DescribeError("send to", "client fd=" + std::to_string(client_fd_), error);
        logger_->Error(error_str);
        return false;
    }
    logger_->Debug("Sent " + std::to_string(total_bytes) + " bytes to client fd=" + std::to_string(client_fd_));
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
            logger_->Info("Client fd=" + std::to_string(client_fd_) + " disconnected gracefully");
            return;
        }
        if (received < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            const int error = errno;
            logger_->Error(DescribeError("read from", "client fd=" + std::to_string(client_fd_), error));
            return;
        }
        logger_->Debug("Received " + std::to_string(received) + " bytes from client fd=" + std::to_string(client_fd_));
        std::vector<std::string> payloads;
        protocol::Error error;
        if (!decoder.Push(std::span(buffer.data(), static_cast<std::size_t>(received)), payloads, error))
        {
            logger_->Warn("Protocol error from client fd=" + std::to_string(client_fd_) + ": " + error.code + " - " + error.message);
            SendAll(protocol::EncodeFrame(protocol::EncodeErrorResponse("", error)));
            return;
        }
        for (const auto& payload : payloads)
        {
            logger_->Debug("Processing request payload (" + std::to_string(payload.size()) + " bytes) for fd=" + std::to_string(client_fd_));
            const auto response = handler_(payload);
            if (!SendAll(protocol::EncodeFrame(response)))
            {
                return;
            }
        }
    }
}

}
