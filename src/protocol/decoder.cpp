#include "gatekeeper/protocol/decoder.h"
#include "gatekeeper/protocol/frame.h"

#include <string_view>

namespace gatekeeper::protocol
{
namespace
{

bool IsValidUtf8(std::string_view text)
{
    for (std::size_t i = 0; i < text.size();)
    {
        const auto byte = static_cast<unsigned char>(text[i]);
        std::size_t count = 0;
        if (byte <= 0x7F) { ++i; continue; }
        if (byte >= 0xC2 && byte <= 0xDF) count = 1;
        else if (byte >= 0xE0 && byte <= 0xEF) count = 2;
        else if (byte >= 0xF0 && byte <= 0xF4) count = 3;
        else return false;
        if (i + count >= text.size()) return false;
        for (std::size_t j = 1; j <= count; ++j)
            if ((static_cast<unsigned char>(text[i + j]) & 0xC0U) != 0x80U) return false;
        if ((byte == 0xE0 && static_cast<unsigned char>(text[i + 1]) < 0xA0) ||
            (byte == 0xED && static_cast<unsigned char>(text[i + 1]) > 0x9F) ||
            (byte == 0xF0 && static_cast<unsigned char>(text[i + 1]) < 0x90) ||
            (byte == 0xF4 && static_cast<unsigned char>(text[i + 1]) > 0x8F)) return false;
        i += count + 1;
    }
    return true;
}

}

bool Decoder::Push(std::span<const std::uint8_t> bytes, std::vector<std::string>& payloads,
                   ProtocolError& error)
{
    buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
    while (buffer_.size() >= 4)
    {
        const auto length = (static_cast<std::uint32_t>(buffer_[0]) << 24U) |
                            (static_cast<std::uint32_t>(buffer_[1]) << 16U) |
                            (static_cast<std::uint32_t>(buffer_[2]) << 8U) | buffer_[3];
        if (length > kMaxFrameSize) { error = {"FRAME_TOO_LARGE", "payload exceeds the 1 MiB GKWP limit"}; return false; }
        const auto frame_size = static_cast<std::size_t>(length) + 4;
        if (buffer_.size() < frame_size) return true;
        std::string payload(buffer_.begin() + 4, buffer_.begin() + static_cast<std::ptrdiff_t>(frame_size));
        if (!IsValidUtf8(payload)) { error = {"INVALID_UTF8", "payload must be valid UTF-8"}; return false; }
        payloads.push_back(std::move(payload));
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(frame_size));
    }
    return true;
}

}
