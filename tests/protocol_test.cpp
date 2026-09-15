#include "protocol.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using gatekeeper::protocol::EncodeError;
using gatekeeper::protocol::EncodeFrame;
using gatekeeper::protocol::FrameDecoder;
using gatekeeper::protocol::ParseRequest;
using gatekeeper::protocol::ProtocolError;
using gatekeeper::protocol::Request;

namespace
{

void TestPartialFrame()
{
    const auto encoded = EncodeFrame(R"({"id":"request-1","op":"PING","body":{}})");
    FrameDecoder decoder;
    ProtocolError error;
    std::vector<std::string> frames;

    assert(decoder.Push(std::span(encoded.data(), 2), frames, error));
    assert(frames.empty());
    assert(decoder.Push(std::span(encoded.data() + 2, encoded.size() - 2), frames, error));
    assert(frames.size() == 1);

    Request request;
    assert(ParseRequest(frames.front(), request, error));
    assert(request.id == "request-1");
    assert(request.op == "PING");
    assert(request.body_json == "{}");
}

void TestPipelinedFrames()
{
    const auto first = EncodeFrame(R"({"id":"1","op":"PING","body":{}})");
    const auto second = EncodeFrame(R"({"id":"2","op":"GET","body":{"key":"name"}})");
    std::vector<std::uint8_t> bytes = first;
    bytes.insert(bytes.end(), second.begin(), second.end());

    FrameDecoder decoder;
    ProtocolError error;
    std::vector<std::string> frames;
    assert(decoder.Push(bytes, frames, error));
    assert(frames.size() == 2);
}

void TestInvalidRequest()
{
    Request request;
    ProtocolError error;
    assert(!ParseRequest(R"({"id":"1","op":"PING","body":[]})", request, error));
    assert(error.code == "INVALID_REQUEST");
}

void TestOversizedFrame()
{
    FrameDecoder decoder;
    ProtocolError error;
    std::vector<std::string> frames;
    const std::vector<std::uint8_t> header{0x00, 0x10, 0x00, 0x01};
    assert(!decoder.Push(header, frames, error));
    assert(error.code == "FRAME_TOO_LARGE");
}

void TestErrorEncoding()
{
    const std::string response = EncodeError("request-1", {"INVALID_REQUEST", "bad input"});
    assert(response ==
           R"({"id":"request-1","ok":false,"error":{"code":"INVALID_REQUEST","message":"bad input"}})");
}

} // namespace

int main()
{
    TestPartialFrame();
    TestPipelinedFrames();
    TestInvalidRequest();
    TestOversizedFrame();
    TestErrorEncoding();
    std::cout << "protocol tests passed\n";
}
