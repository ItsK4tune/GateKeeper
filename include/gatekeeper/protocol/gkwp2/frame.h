#pragma once

#include "gatekeeper/protocol/gkwp2/header.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gatekeeper::protocol::gkwp2
{

struct Frame
{
    Header header{};
    std::string payload;

    [[nodiscard]] bool IsComplete() const noexcept
    {
        return payload.size() == header.payload_len;
    }
};

std::vector<std::uint8_t> EncodeFrame(const Header& header, std::string_view payload);
std::vector<std::uint8_t> EncodeResponse(std::uint64_t request_id, std::uint32_t stream_id, std::string_view payload, bool end_stream = false);
std::vector<std::uint8_t> EncodeError(std::uint64_t request_id, std::uint32_t stream_id, std::string_view error_code, std::string_view message, bool end_stream = true);
std::vector<std::uint8_t> EncodeEvent(std::uint64_t request_id, std::uint32_t stream_id, std::string_view event_type, std::string_view data);

}
