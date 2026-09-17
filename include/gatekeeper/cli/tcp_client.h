#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace gatekeeper::cli
{

class TcpClient
{
public:
    TcpClient(std::string host, std::uint16_t port);
    ~TcpClient();
    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;
    void Open();
    std::string Execute(const std::string& operation, const std::string& body = "{}");

private:
    void Close();
    void ReadAll(std::span<std::uint8_t> bytes);
    void SendAll(std::span<const std::uint8_t> bytes);

    std::string host_;
    std::uint16_t port_;
    int socket_fd_ = -1;
    std::uint64_t next_id_ = 1;
};

}
