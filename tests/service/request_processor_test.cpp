#include "gatekeeper/service/request_processor.h"
#include "gatekeeper/command/command_dispatcher.h"

#include <iostream>
#include <stdexcept>

namespace
{

void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

}

int main()
{
    try
    {
        gatekeeper::command::CommandDispatcher dispatcher;
        int calls = 0;
        dispatcher.Register("CUSTOM", [&calls](const auto& request) {
            ++calls;
            Require(request.body_json == R"({"key":"Value"})", "dispatcher modified body");
            return gatekeeper::command::DispatchResult{true, R"({"custom":true})", {}};
        });
        gatekeeper::service::RequestProcessor processor([&dispatcher](const auto& request) {
            return dispatcher.Dispatch(request);
        });
        Require(processor.Process(R"({"id":"1","op":"cUsToM","body":{"key":"Value"}})") ==
                R"({"id":"1","ok":true,"result":{"custom":true}})", "custom handler not used");
        Require(calls == 1, "wrong dispatch count");
        Require(processor.Process(R"({"id":"2","op":"PING","body":{}})") ==
                R"({"id":"2","ok":true,"result":{"pong":true}})", "PING changed");
        Require(processor.Process(R"({"id":"3","op":"CUSTOM","body":[]})").find("INVALID_REQUEST") !=
                std::string::npos && calls == 1, "invalid request reached handler");
        Require(processor.Process(R"({"id":"4","op":"unknown","body":{}})").find("UNKNOWN_COMMAND") !=
                std::string::npos, "unknown command accepted");
        bool duplicate_rejected = false;
        try
        {
            dispatcher.Register("ping", [](const auto&) { return gatekeeper::command::DispatchResult{}; });
        }
        catch (const std::invalid_argument&)
        {
            duplicate_rejected = true;
        }
        Require(duplicate_rejected, "duplicate registration accepted");
        std::cout << "Request processor and dispatcher extension tests passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
