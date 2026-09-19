#pragma once

#include <cstdint>
#include <span>

namespace gatekeeper::protocol::gkwp2
{

inline constexpr std::uint16_t kMagic = 0x474B; // 'G' 'K'
inline constexpr std::uint8_t kVersion = 0x02;

enum class MsgType : std::uint8_t
{
    Request = 0x01,
    Response = 0x02,
    Event = 0x03,
    Error = 0x04,
};

namespace flags
{
inline constexpr std::uint8_t kCompressed = 0x01;
inline constexpr std::uint8_t kEncrypted = 0x02;
inline constexpr std::uint8_t kMoreFragments = 0x04;
inline constexpr std::uint8_t kEndStream = 0x08;
inline constexpr std::uint8_t kResetStream = 0x10;
}

#pragma pack(push, 1)
struct Header
{
    std::uint16_t magic;
    std::uint8_t version;
    std::uint8_t flags;
    std::uint8_t msg_type;
    std::uint8_t reserved[3];
    std::uint64_t request_id;
    std::uint32_t stream_id;
    std::uint32_t payload_len;
};
#pragma pack(pop)

static_assert(sizeof(Header) == 24, "GKWP/2 Header must be exactly 24 bytes");

inline constexpr std::size_t kHeaderSize = 24;
inline constexpr std::uint32_t kMaxPayloadLength = 16 * 1024 * 1024;
inline constexpr std::uint32_t kMaxFragmentSize = 16 * 1024;

}
