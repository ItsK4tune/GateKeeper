#include "gatekeeper/command/dispatcher.h"

#include <cassert>

namespace
{

void TestPingReturnsPong()
{
    gatekeeper::command::Dispatcher dispatcher;
    const auto response = dispatcher.Dispatch({"request-1", "PING", "{}"});

    assert(response.ok);
    assert(response.result_json == R"({"pong":true})");
}

void TestPingIgnoresCommandCase()
{
    gatekeeper::command::Dispatcher dispatcher;
    const auto response = dispatcher.Dispatch({"request-1", "pInG", "{}"});

    assert(response.ok);
}

void TestUnknownCommandReturnsAnError()
{
    gatekeeper::command::Dispatcher dispatcher;
    const auto response = dispatcher.Dispatch({"request-1", "UNKNOWN", "{}"});

    assert(!response.ok);
    assert(response.error.code == "UNKNOWN_COMMAND");
}

void TestSetStoresAValueThatGetReturns()
{
    gatekeeper::command::Dispatcher dispatcher;

    const auto set = dispatcher.Dispatch({"set-1", "SET", R"({"key":"name","value":"duong"})"});
    const auto get = dispatcher.Dispatch({"get-1", "GET", R"({"key":"name"})"});

    assert(set.ok);
    assert(set.result_json == R"({"stored":true})");
    assert(get.ok);
    assert(get.result_json == R"({"value":"duong"})");
}

void TestGetMissingKeyReturnsNotFound()
{
    gatekeeper::command::Dispatcher dispatcher;
    const auto response = dispatcher.Dispatch({"get-1", "GET", R"({"key":"missing"})"});

    assert(!response.ok);
    assert(response.error.code == "KEY_NOT_FOUND");
}

}

void RunCommandDispatcherTests()
{
    TestPingReturnsPong();
    TestPingIgnoresCommandCase();
    TestUnknownCommandReturnsAnError();
    TestSetStoresAValueThatGetReturns();
    TestGetMissingKeyReturnsNotFound();
}
