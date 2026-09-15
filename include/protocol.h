#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gatekeeper::protocol
{

inline constexpr std::size_t kMaxFrameSize = 1024U * 1024U;

struct Request
{
    std::string id;
    std::string op;
    std::string body_json;
};

struct ProtocolError
{
    std::string code;
    std::string message;
};

// Accumulates arbitrary TCP chunks and emits complete GKWP/1 JSON payloads.
class FrameDecoder
{
  public:
    bool Push(std::span<const std::uint8_t> bytes, std::vector<std::string>& frames,
              ProtocolError& error);

  private:
    std::vector<std::uint8_t> buffer_;
};

bool ParseRequest(const std::string& payload, Request& request, ProtocolError& error);

std::vector<std::uint8_t> EncodeFrame(const std::string& payload);
std::string EncodeSuccess(const std::string& id, const std::string& result_json);
std::string EncodeError(const std::string& id, const ProtocolError& error);

} // namespace gatekeeper::protocol
