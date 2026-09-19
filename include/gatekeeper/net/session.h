#pragma once

#include "gatekeeper/protocol/gkwp/decoder.h"
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

enum class ProtocolType : std::uint8_t
{
    Unknown = 0,
    Gkwp1 = 1,
    Gkwp2 = 2,
};

struct InboundMessage
{
    ProtocolType protocol{ProtocolType::Unknown};
    std::uint64_t request_id{0};
    std::uint32_t stream_id{0};
    std::uint8_t flags{0};
    std::string payload;
};

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

    [[nodiscard]] ProtocolType Protocol() const noexcept { return protocol_; }

    bool Push(std::span<const std::uint8_t> bytes, std::vector<InboundMessage>& messages, std::string& error_msg)
    {
        if (protocol_ == ProtocolType::Unknown)
        {
            detect_buffer_.insert(detect_buffer_.end(), bytes.begin(), bytes.end());
            if (detect_buffer_.size() < 2)
            {
                return true;
            }

            if (detect_buffer_[0] == 0x47 && detect_buffer_[1] == 0x4B)
            {
                protocol_ = ProtocolType::Gkwp2;
            }
            else
            {
                protocol_ = ProtocolType::Gkwp1;
            }

            std::vector<std::uint8_t> initial;
            initial.swap(detect_buffer_);
            return ProcessBytes(std::span(initial.data(), initial.size()), messages, error_msg);
        }

        return ProcessBytes(bytes, messages, error_msg);
    }

    protocol::Decoder& Decoder() noexcept { return gkwp1_decoder_; }
    protocol::gkwp2::Decoder& Gkwp2Decoder() noexcept { return gkwp2_decoder_; }
    protocol::gkwp2::StreamManager& StreamManager() noexcept { return stream_manager_; }

private:
    bool ProcessBytes(std::span<const std::uint8_t> bytes, std::vector<InboundMessage>& messages, std::string& error_msg)
    {
        if (protocol_ == ProtocolType::Gkwp2)
        {
            std::vector<protocol::gkwp2::Frame> frames;
            if (!gkwp2_decoder_.Push(bytes, frames, error_msg))
            {
                return false;
            }
            for (auto& frame : frames)
            {
                InboundMessage msg;
                msg.protocol = ProtocolType::Gkwp2;
                msg.request_id = frame.header.request_id;
                msg.stream_id = frame.header.stream_id;
                msg.flags = frame.header.flags;
                msg.payload = std::move(frame.payload);
                messages.push_back(std::move(msg));
            }
            return true;
        }

        std::vector<std::string> payloads;
        protocol::Error err;
        if (!gkwp1_decoder_.Push(bytes, payloads, err))
        {
            error_msg = err.message;
            return false;
        }
        for (auto& payload : payloads)
        {
            InboundMessage msg;
            msg.protocol = ProtocolType::Gkwp1;
            msg.payload = std::move(payload);
            messages.push_back(std::move(msg));
        }
        return true;
    }

    std::uint64_t id_{0};
    int fd_{-1};
    std::string address_;
    std::uint64_t requests_count_{0};
    std::uint64_t bytes_received_{0};
    std::uint64_t bytes_sent_{0};
    ProtocolType protocol_{ProtocolType::Unknown};
    std::vector<std::uint8_t> detect_buffer_;
    protocol::Decoder gkwp1_decoder_;
    protocol::gkwp2::Decoder gkwp2_decoder_;
    protocol::gkwp2::StreamManager stream_manager_;
};

}
