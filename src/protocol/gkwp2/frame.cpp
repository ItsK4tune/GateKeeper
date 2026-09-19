#include "gatekeeper/protocol/gkwp2/frame.h"
#include <arpa/inet.h>
#include <cstring>
#include <endian.h>

namespace gatekeeper::protocol::gkwp2
{

std::vector<std::uint8_t> EncodeFrame(const Header& header, std::string_view payload)
{
    std::vector<std::uint8_t> bytes(kHeaderSize + payload.size());

    Header net_header = header;
    net_header.magic = htons(header.magic);
    net_header.request_id = htobe64(header.request_id);
    net_header.stream_id = htonl(header.stream_id);
    net_header.payload_len = htonl(static_cast<std::uint32_t>(payload.size()));

    std::memcpy(bytes.data(), &net_header, kHeaderSize);
    if (!payload.empty())
    {
        std::memcpy(bytes.data() + kHeaderSize, payload.data(), payload.size());
    }
    return bytes;
}

std::vector<std::uint8_t> EncodeResponse(std::uint64_t request_id, std::uint32_t stream_id, std::string_view payload, bool end_stream)
{
    Header hdr{};
    hdr.magic = kMagic;
    hdr.version = kVersion;
    hdr.flags = end_stream ? flags::kEndStream : 0;
    hdr.msg_type = static_cast<std::uint8_t>(MsgType::Response);
    hdr.request_id = request_id;
    hdr.stream_id = stream_id;
    hdr.payload_len = static_cast<std::uint32_t>(payload.size());
    return EncodeFrame(hdr, payload);
}

std::vector<std::uint8_t> EncodeError(std::uint64_t request_id, std::uint32_t stream_id, std::string_view error_code, std::string_view message, bool end_stream)
{
    Header hdr{};
    hdr.magic = kMagic;
    hdr.version = kVersion;
    hdr.flags = end_stream ? flags::kEndStream : 0;
    hdr.msg_type = static_cast<std::uint8_t>(MsgType::Error);
    hdr.request_id = request_id;
    hdr.stream_id = stream_id;

    std::string err_payload = "{\"code\":\"" + std::string(error_code) + "\",\"message\":\"" + std::string(message) + "\"}";
    hdr.payload_len = static_cast<std::uint32_t>(err_payload.size());
    return EncodeFrame(hdr, err_payload);
}

std::vector<std::uint8_t> EncodeEvent(std::uint64_t request_id, std::uint32_t stream_id, std::string_view event_type, std::string_view data)
{
    Header hdr{};
    hdr.magic = kMagic;
    hdr.version = kVersion;
    hdr.flags = 0;
    hdr.msg_type = static_cast<std::uint8_t>(MsgType::Event);
    hdr.request_id = request_id;
    hdr.stream_id = stream_id;

    std::string evt_payload = "{\"event_type\":\"" + std::string(event_type) + "\",\"data\":" + (data.empty() ? "{}" : std::string(data)) + "}";
    hdr.payload_len = static_cast<std::uint32_t>(evt_payload.size());
    return EncodeFrame(hdr, evt_payload);
}

}
