#include "gatekeeper/command/command_dispatcher.h"

#include <cassert>

namespace
{

void TestPingReturnsPong()
{
    gatekeeper::command::CommandDispatcher dispatcher;
    const auto response = dispatcher.Dispatch({"request-1", "PING", "{}"});

    assert(response.ok);
    assert(response.result_json == R"({"pong":true})");
}

void TestPingIgnoresCommandCase()
{
    gatekeeper::command::CommandDispatcher dispatcher;
    const auto response = dispatcher.Dispatch({"request-1", "pInG", "{}"});

    assert(response.ok);
}

void TestUnknownCommandReturnsAnError()
{
    gatekeeper::command::CommandDispatcher dispatcher;
    const auto response = dispatcher.Dispatch({"request-1", "GET", "{}"});

    assert(!response.ok);
    assert(response.error.code == "UNKNOWN_COMMAND");
}

}

void RunCommandDispatcherTests()
{
    TestPingReturnsPong();
    TestPingIgnoresCommandCase();
    TestUnknownCommandReturnsAnError();
}
