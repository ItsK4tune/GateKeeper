#include "server.h"
#include "protocol.h"

#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <iostream>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace gatekeeper
{
namespace
{

bool SendAll(int client_fd, const std::vector<std::uint8_t>& bytes)
{
    std::size_t sent = 0;
    while (sent < bytes.size())
    {
        const ssize_t written = send(client_fd, bytes.data() + sent, bytes.size() - sent, 0);
        if (written > 0)
        {
            sent += static_cast<std::size_t>(written);
            continue;
        }
        if (written == -1 && errno == EINTR)
        {
            continue;
        }
        return false;
    }
    return true;
}

bool SendError(int client_fd, const std::string& id, const protocol::ProtocolError& error)
{
    return SendAll(client_fd, protocol::EncodeFrame(protocol::EncodeError(id, error)));
}

} // namespace

Server::Server(int port) : port(port), server_fd(-1) {}

void Server::Run()
{
    SetupSocket();

    while (true)
    {
        AcceptSocket();
    }
}

void Server::SetupSocket()
{
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd == -1)
    {
        throw std::runtime_error("Failed to create socket");
    }

    sockaddr_in address{};

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == -1)
    {
        close(server_fd);

        throw std::runtime_error("Failed to bind socket");
    }

    if (listen(server_fd, 16) == -1)
    {
        close(server_fd);

        throw std::runtime_error("Failed to listen");
    }

    std::cout << "GateKeeper server listening on port " << port << '\n';
}

void Server::AcceptSocket()
{
    sockaddr_in client_address{};

    socklen_t client_length = sizeof(client_address);

    int client_fd = accept(server_fd, reinterpret_cast<sockaddr*>(&client_address), &client_length);

    if (client_fd == -1)
    {
        throw std::runtime_error("Failed to accept client");
    }

    std::cout << "Client connected\n";

    protocol::FrameDecoder decoder;
    std::array<std::uint8_t, 4096> buffer{};

    while (true)
    {
        const ssize_t bytes_read = recv(client_fd, buffer.data(), buffer.size(), 0);
        if (bytes_read == 0)
        {
            break;
        }
        if (bytes_read == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }
            std::cerr << "Client read failed\n";
            break;
        }

        std::vector<std::string> frames;
        protocol::ProtocolError frame_error;
        if (!decoder.Push(std::span(buffer.data(), static_cast<std::size_t>(bytes_read)), frames, frame_error))
        {
            SendError(client_fd, "", frame_error);
            break;
        }

        for (const auto& frame : frames)
        {
            protocol::Request request;
            protocol::ProtocolError request_error;
            if (!protocol::ParseRequest(frame, request, request_error))
            {
                if (!SendError(client_fd, request.id, request_error))
                {
                    break;
                }
                continue;
            }

            // Commands are introduced in Step 3. Step 2 only validates and frames GKWP requests.
            const protocol::ProtocolError unavailable{"COMMAND_UNAVAILABLE", "command dispatch is not implemented yet"};
            if (!SendError(client_fd, request.id, unavailable))
            {
                break;
            }
        }
    }

    close(client_fd);
}

} // namespace gatekeeper
