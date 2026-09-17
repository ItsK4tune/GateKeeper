#pragma once

#include "gatekeeper/protocol/decoder.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace gatekeeper::net
{

class Session
{
public:
    Session(std::uint64_t id, int fd, std::string address = "")
        : id_(id), fd_(fd), address_(std::move(address))
    {
    }

    [[nodiscard]] std::uint64_t Id() const noexcept { return id_; }
    [[nodiscard]] int Fd() const noexcept { return fd_; }
    [[nodiscard]] const std::string& Address() const noexcept { return address_; }

    [[nodiscard]] std::uint64_t RequestsCount() const noexcept { return requests_count_; }
    [[nodiscard]] std::uint64_t BytesReceived() const noexcept { return bytes_received_; }
    [[nodiscard]] std::uint64_t BytesSent() const noexcept { return bytes_sent_; }

    void IncrementRequests() noexcept { ++requests_count_; }
    void AddBytesReceived(std::size_t bytes) noexcept { bytes_received_ += bytes; }
    void AddBytesSent(std::size_t bytes) noexcept { bytes_sent_ += bytes; }

    protocol::Decoder& Decoder() noexcept { return decoder_; }

private:
    std::uint64_t id_{0};
    int fd_{-1};
    std::string address_;
    std::uint64_t requests_count_{0};
    std::uint64_t bytes_received_{0};
    std::uint64_t bytes_sent_{0};
    protocol::Decoder decoder_;
};

}
