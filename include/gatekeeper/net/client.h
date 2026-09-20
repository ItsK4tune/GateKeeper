#pragma once

#include "gatekeeper/core/log/logger.h"
#include "gatekeeper/protocol/gkwp2/binary_codec.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace gatekeeper::net
{

class Client
{
public:
    Client(std::string host, std::uint16_t port, std::shared_ptr<log::Logger> logger = log::Logger::Null());
    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    void Open();
    void Close();

    std::string ExecuteBinary(std::span<const std::uint8_t> binary_payload);
    std::string Execute(const std::string& operation, const std::string& body);

private:
    void ReadAll(std::span<std::uint8_t> bytes);
    void SendAll(std::span<const std::uint8_t> bytes);

    std::string host_;
    std::uint16_t port_;
    std::shared_ptr<log::Logger> logger_;
    int socket_fd_{-1};
    std::uint64_t next_id_{1};
};

}
