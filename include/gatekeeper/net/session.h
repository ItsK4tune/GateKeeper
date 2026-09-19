#pragma once

#include "gatekeeper/protocol/gkwp2/decoder.h"
#include "gatekeeper/protocol/gkwp2/stream_manager.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

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

    bool Push(std::span<const std::uint8_t> bytes, std::vector<protocol::gkwp2::Frame>& frames, std::string& error_msg)
    {
        return decoder_.Push(bytes, frames, error_msg);
    }

    protocol::gkwp2::Decoder& Decoder() noexcept { return decoder_; }
    protocol::gkwp2::StreamManager& StreamManager() noexcept { return stream_manager_; }

private:
    std::uint64_t id_{0};
    int fd_{-1};
    std::string address_;
    std::uint64_t requests_count_{0};
    std::uint64_t bytes_received_{0};
    std::uint64_t bytes_sent_{0};
    protocol::gkwp2::Decoder decoder_;
    protocol::gkwp2::StreamManager stream_manager_;
};

}
