#include "gatekeeper/protocol/response.h"

#include <cassert>

namespace
{

void TestErrorResponseUsesTheProtocolErrorShape()
{
    const gatekeeper::protocol::ProtocolError error{"INVALID_REQUEST", "bad input"};
    const auto payload = gatekeeper::protocol::EncodeErrorResponse("request-1", error);

    assert(payload ==
           R"({"id":"request-1","ok":false,"error":{"code":"INVALID_REQUEST","message":"bad input"}})");
}

}

void RunFrameDecoderTests();
void RunJsonRequestParserTests();
void RunCommandDispatcherTests();
void RunCliCommandTests();

int main()
{
    RunFrameDecoderTests();
    RunJsonRequestParserTests();
    RunCommandDispatcherTests();
    RunCliCommandTests();
    TestErrorResponseUsesTheProtocolErrorShape();
}
