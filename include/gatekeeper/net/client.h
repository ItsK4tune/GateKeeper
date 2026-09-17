#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace gatekeeper::net
{

class Client
{
public:
    Client(std::string host, std::uint16_t port);
    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    void Open();
    void Close();
    std::string Execute(const std::string& operation, const std::string& body = "{}");

private:
    std::string host_;
    std::uint16_t port_;
    int socket_fd_ = -1;
    std::uint64_t next_id_ = 1;

    void SendAll(std::span<const std::uint8_t> bytes);
    void ReadAll(std::span<std::uint8_t> bytes);
};

using TcpClient = Client;

}

namespace gatekeeper::cli
{
using TcpClient = net::Client;
}
