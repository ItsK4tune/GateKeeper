#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gatekeeper::protocol::resp
{

struct Command
{
    std::vector<std::string> args;
};

class Decoder
{
public:
    Decoder() = default;

    bool Push(std::span<const std::uint8_t> bytes, std::vector<Command>& commands, std::string& error_msg);
    void Reset();

private:
    std::vector<std::uint8_t> buffer_;
    std::size_t read_offset_{0};
};

}
