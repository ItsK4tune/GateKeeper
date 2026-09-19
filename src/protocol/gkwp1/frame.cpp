#include "gatekeeper/protocol/gkwp1/frame.h"

#include <limits>

namespace gatekeeper::protocol
{

std::vector<std::uint8_t> EncodeFrame(const std::string& payload)
{
    if (payload.size() > kMaxFrameSize || payload.size() > std::numeric_limits<std::uint32_t>::max())
    {
        return {};
    }

    const auto length = static_cast<std::uint32_t>(payload.size());
    std::vector<std::uint8_t> frame;
    frame.reserve(sizeof(length) + payload.size());
    frame.push_back(static_cast<std::uint8_t>(length >> 24U));
    frame.push_back(static_cast<std::uint8_t>(length >> 16U));
    frame.push_back(static_cast<std::uint8_t>(length >> 8U));
    frame.push_back(static_cast<std::uint8_t>(length));
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

}
