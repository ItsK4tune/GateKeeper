#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gatekeeper::protocol
{

inline constexpr std::size_t kMaxFrameSize = 1024U * 1024U;

std::vector<std::uint8_t> EncodeFrame(const std::string& payload);

}
