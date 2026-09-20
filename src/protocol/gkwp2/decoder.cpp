#include "gatekeeper/protocol/gkwp2/decoder.h"
#include <arpa/inet.h>
#include <cstring>
#include <endian.h>

namespace gatekeeper::protocol::gkwp2
{

bool Decoder::Push(std::span<const std::uint8_t> incoming, std::vector<Frame>& out_frames, std::string& err_msg)
{
    if (buffer_.size() + incoming.size() > kMaxDecoderBufferSize)
    {
        err_msg = "Incoming buffer limit exceeded";
        return false;
    }

    buffer_.insert(buffer_.end(), incoming.begin(), incoming.end());

    while (buffer_.size() - read_offset_ >= kHeaderSize)
    {
        Header net_header{};
        std::memcpy(&net_header, buffer_.data() + read_offset_, kHeaderSize);

        Header hdr{};
        hdr.magic = ntohs(net_header.magic);
        hdr.version = net_header.version;
        hdr.flags = net_header.flags;
        hdr.msg_type = net_header.msg_type;
        std::memcpy(hdr.reserved, net_header.reserved, sizeof(hdr.reserved));
        hdr.request_id = be64toh(net_header.request_id);
        hdr.stream_id = ntohl(net_header.stream_id);
        hdr.payload_len = ntohl(net_header.payload_len);

        if (hdr.magic != kMagic)
        {
            err_msg = "Invalid GKWP/2 magic bytes";
            return false;
        }
        if (hdr.version != kVersion)
        {
            err_msg = "Unsupported GKWP/2 version";
            return false;
        }
        if (hdr.payload_len > kMaxPayloadLength)
        {
            err_msg = "Payload length exceeds maximum allowed limit";
            return false;
        }

        const std::size_t total_frame_len = kHeaderSize + hdr.payload_len;
        if (buffer_.size() - read_offset_ < total_frame_len)
        {
            break;
        }

        Frame frame{};
        frame.header = hdr;
        if (hdr.payload_len > 0)
        {
            frame.payload.assign(
                reinterpret_cast<const char*>(buffer_.data() + read_offset_ + kHeaderSize),
                hdr.payload_len);
        }

        out_frames.push_back(std::move(frame));
        read_offset_ += total_frame_len;
    }

    if (read_offset_ > 0)
    {
        if (read_offset_ == buffer_.size())
        {
            buffer_.clear();
            read_offset_ = 0;
        }
        else if (read_offset_ > 65536)
        {
            buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(read_offset_));
            read_offset_ = 0;
        }
    }

    return true;
}

void Decoder::Reset()
{
    buffer_.clear();
    read_offset_ = 0;
}

}
