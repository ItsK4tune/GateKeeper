#include "gatekeeper/protocol/gkwp2/header.h"
#include "gatekeeper/protocol/gkwp2/frame.h"
#include "gatekeeper/protocol/gkwp2/decoder.h"
#include "gatekeeper/protocol/gkwp2/stream_manager.h"

#include <cassert>
#include <iostream>

using namespace gatekeeper::protocol::gkwp2;

void TestHeaderSizeAndAlignment()
{
    static_assert(sizeof(Header) == 24);
    assert(sizeof(Header) == 24);
    assert(kHeaderSize == 24);

    Header hdr{};
    hdr.magic = kMagic;
    hdr.version = kVersion;
    hdr.request_id = 0x123456789ABCDEF0ULL;
    hdr.stream_id = 1;
    hdr.payload_len = 100;

    assert(hdr.magic == 0x474B);
    assert(hdr.version == 2);
}

void TestEncodeAndDecode()
{
    Decoder decoder;
    std::string payload = R"({"op":"PING"})";
    auto encoded = EncodeResponse(1001, 1, payload, true);

    assert(encoded.size() == 24 + payload.size());

    std::vector<Frame> frames;
    std::string err;
    bool ok = decoder.Push(encoded, frames, err);
    assert(ok);
    assert(frames.size() == 1);

    const auto& f = frames[0];
    assert(f.header.magic == kMagic);
    assert(f.header.version == kVersion);
    assert(f.header.request_id == 1001);
    assert(f.header.stream_id == 1);
    assert((f.header.flags & flags::kEndStream) != 0);
    assert(f.header.msg_type == static_cast<std::uint8_t>(MsgType::Response));
    assert(f.payload == payload);
}

void TestStreamManagerConcurrencyAndTimeout()
{
    StreamManager sm(2, 100);

    assert(sm.CanOpenStream(1));
    auto* s1 = sm.GetOrCreateStream(1, 1000);
    assert(s1 != nullptr);
    assert(sm.ActiveStreamCount() == 1);

    assert(sm.CanOpenStream(3));
    auto* s3 = sm.GetOrCreateStream(3, 1000);
    assert(s3 != nullptr);
    assert(sm.ActiveStreamCount() == 2);

    assert(!sm.CanOpenStream(5));
    auto* s5 = sm.GetOrCreateStream(5, 1000);
    assert(s5 == nullptr);

    s1->last_activity_ms = 1150;
    std::size_t purged = sm.PurgeIdleStreams(1120);
    assert(purged == 1);
    assert(sm.ActiveStreamCount() == 1);
    assert(sm.FindStream(1) != nullptr);
    assert(sm.FindStream(3) == nullptr);
}

int main()
{
    TestHeaderSizeAndAlignment();
    TestEncodeAndDecode();
    TestStreamManagerConcurrencyAndTimeout();
    std::cout << "All GKWP/2 tests passed!" << std::endl;
    return 0;
}
