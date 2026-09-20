#pragma once

#include "gatekeeper/protocol/gkwp2/header.h"
#include "gatekeeper/protocol/gkwp2/frame.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gatekeeper::protocol::gkwp2
{

class Decoder
{
public:
    static constexpr std::size_t kMaxDecoderBufferSize = 16 * 1024 * 1024;

    Decoder() = default;

    bool Push(std::span<const std::uint8_t> incoming, std::vector<Frame>& out_frames, std::string& err_msg);

    void Reset();

private:
    std::vector<std::uint8_t> buffer_;
    std::size_t read_offset_{0};
};

}
