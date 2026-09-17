#pragma once

#include "gatekeeper/protocol/error.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gatekeeper::protocol
{

class Decoder
{
public:
    bool Push(std::span<const std::uint8_t> bytes, std::vector<std::string>& payloads,
              Error& error);

private:
    std::vector<std::uint8_t> buffer_;
};

using FrameDecoder = Decoder;

}
