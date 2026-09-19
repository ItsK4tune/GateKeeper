#include "gatekeeper/net/connection.h"
#include "gatekeeper/net/error.h"
#include "gatekeeper/protocol/gkwp/frame.h"
#include "gatekeeper/protocol/gkwp/response.h"
#include "gatekeeper/protocol/gkwp2/frame.h"

#include <array>
#include <cerrno>
#include <sys/socket.h>
#include <unistd.h>

namespace gatekeeper::net
{

Connection::Connection(int client_fd, const RequestHandler& handler, std::shared_ptr<log::Logger> logger, std::uint64_t session_id, std::string address)
    : client_fd_(client_fd), handler_(handler), logger_(std::move(logger)), session_(session_id, client_fd, std::move(address))
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
    session_.AddBytesSent(total_bytes);
    logger_->Debug("Sent " + std::to_string(total_bytes) + " bytes to client fd=" + std::to_string(client_fd_));
    return true;
}

void Connection::Serve()
{
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
        session_.AddBytesReceived(static_cast<std::size_t>(received));
        logger_->Debug("Received " + std::to_string(received) + " bytes from client fd=" + std::to_string(client_fd_));
        std::vector<InboundMessage> messages;
        std::string error;
        if (!session_.Push(std::span(buffer.data(), static_cast<std::size_t>(received)), messages, error))
        {
            if (session_.Protocol() == ProtocolType::Gkwp2)
            {
                const auto err_frame = protocol::gkwp2::EncodeError(0, 0, "PROTOCOL_ERROR", error, true);
                logger_->Warn("Protocol error from client fd=" + std::to_string(client_fd_) + ": " + error);
                SendAll(err_frame);
            }
            else
            {
                const auto err_response = protocol::EncodeErrorResponse("", {"PROTOCOL_ERROR", error});
                logger_->Warn("Protocol error from client fd=" + std::to_string(client_fd_) + ": " + error);
                logger_->Info("Sending GKWP error response to client fd=" + std::to_string(client_fd_) + ": " + err_response);
                SendAll(protocol::EncodeFrame(err_response));
            }
            return;
        }
        for (const auto& msg : messages)
        {
            session_.IncrementRequests();
            logger_->Debug("Processing request payload (" + std::to_string(msg.payload.size()) + " bytes) for fd=" + std::to_string(client_fd_));
            const auto response = handler_(msg.payload);
            if (msg.protocol == ProtocolType::Gkwp2)
            {
                logger_->Info("Sending GKWP/2 response to client fd=" + std::to_string(client_fd_) + ": " + response);
                const auto resp_frame = protocol::gkwp2::EncodeResponse(
                    msg.request_id, msg.stream_id, response,
                    (msg.flags & protocol::gkwp2::flags::kEndStream) != 0);
                if (!SendAll(resp_frame))
                {
                    return;
                }
            }
            else
            {
                logger_->Info("Sending GKWP response to client fd=" + std::to_string(client_fd_) + ": " + response);
                if (!SendAll(protocol::EncodeFrame(response)))
                {
                    return;
                }
            }
        }
    }
}

}
