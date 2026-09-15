#include "gatekeeper/net/client_connection.h"

#include "gatekeeper/protocol/frame.h"
#include "gatekeeper/protocol/frame_decoder.h"
#include "gatekeeper/protocol/json_request_parser.h"
#include "gatekeeper/protocol/response.h"

#include <array>
#include <cerrno>
#include <sys/socket.h>
#include <unistd.h>

namespace gatekeeper::net
{

ClientConnection::ClientConnection(int client_fd) : client_fd_(client_fd) {}

bool ClientConnection::SendAll(const unsigned char* bytes, unsigned long size)
{
    unsigned long sent = 0;
    while (sent < size)
    {
        const auto written = send(client_fd_, bytes + sent, size - sent, 0);
        if (written > 0) { sent += static_cast<unsigned long>(written); continue; }
        if (written == -1 && errno == EINTR) continue;
        return false;
    }
    return true;
}

bool ClientConnection::SendError(const char* id, const char* code, const char* message)
{
    const auto payload = protocol::EncodeErrorResponse(id, {code, message});
    const auto frame = protocol::EncodeFrame(payload);
    return SendAll(frame.data(), frame.size());
}

void ClientConnection::Serve()
{
    protocol::FrameDecoder decoder;
    protocol::JsonRequestParser parser;
    std::array<std::uint8_t, 4096> buffer{};
    while (true)
    {
        const auto received = recv(client_fd_, buffer.data(), buffer.size(), 0);
        if (received <= 0) break;
        std::vector<std::string> payloads;
        protocol::ProtocolError error;
        if (!decoder.Push(std::span(buffer.data(), static_cast<std::size_t>(received)), payloads, error)) { SendError("", error.code.c_str(), error.message.c_str()); break; }
        for (const auto& payload : payloads)
        {
            protocol::Request request;
            if (!parser.Parse(payload, request, error)) { if (!SendError(request.id.c_str(), error.code.c_str(), error.message.c_str())) break; continue; }
            if (!SendError(request.id.c_str(), "COMMAND_UNAVAILABLE", "command dispatch is not implemented yet")) break;
        }
    }
    close(client_fd_);
}

}
