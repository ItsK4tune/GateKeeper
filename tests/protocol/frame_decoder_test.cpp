#include "gatekeeper/protocol/frame.h"
#include "gatekeeper/protocol/decoder.h"

#include <cassert>
#include <cstdint>
#include <vector>

namespace
{

void TestDecoderEmitsAFrameSplitAcrossTcpReads()
{
    const auto packet = gatekeeper::protocol::EncodeFrame(R"({"id":"request-1","op":"PING","body":{}})");
    gatekeeper::protocol::Decoder decoder;
    gatekeeper::protocol::ProtocolError error;
    std::vector<std::string> payloads;

    assert(decoder.Push(std::span(packet.data(), 2), payloads, error));
    assert(payloads.empty());
    assert(decoder.Push(std::span(packet.data() + 2, packet.size() - 2), payloads, error));
    assert(payloads.size() == 1);
    assert(payloads.front() == R"({"id":"request-1","op":"PING","body":{}})");
}

void TestDecoderEmitsTwoPipelinedFrames()
{
    const auto first = gatekeeper::protocol::EncodeFrame(R"({"id":"1","op":"PING","body":{}})");
    const auto second = gatekeeper::protocol::EncodeFrame(R"({"id":"2","op":"GET","body":{"key":"name"}})");
    std::vector<std::uint8_t> bytes = first;
    bytes.insert(bytes.end(), second.begin(), second.end());

    gatekeeper::protocol::Decoder decoder;
    gatekeeper::protocol::ProtocolError error;
    std::vector<std::string> payloads;

    assert(decoder.Push(bytes, payloads, error));
    assert(payloads.size() == 2);
    assert(payloads[0] == R"({"id":"1","op":"PING","body":{}})");
    assert(payloads[1] == R"({"id":"2","op":"GET","body":{"key":"name"}})");
}

void TestDecoderRejectsAnOversizedFrame()
{
    gatekeeper::protocol::Decoder decoder;
    gatekeeper::protocol::ProtocolError error;
    std::vector<std::string> payloads;
    const std::vector<std::uint8_t> header{0x00, 0x10, 0x00, 0x01};

    assert(!decoder.Push(header, payloads, error));
    assert(error.code == "FRAME_TOO_LARGE");
}

}

void RunFrameDecoderTests()
{
    TestDecoderEmitsAFrameSplitAcrossTcpReads();
    TestDecoderEmitsTwoPipelinedFrames();
    TestDecoderRejectsAnOversizedFrame();
}
