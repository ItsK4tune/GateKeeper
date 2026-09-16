#include "gatekeeper/command/command_dispatcher.h"
#include "gatekeeper/command/name.h"

#include <stdexcept>
#include <utility>

namespace gatekeeper::command
{

CommandDispatcher::CommandDispatcher()
{
    Register("PING", [](const protocol::Request&) {
        return DispatchResult{true, R"({"pong":true})", {}};
    });
}

void CommandDispatcher::Register(std::string name, Handler handler)
{
    name = NormalizeName(name);
    if (name.empty() || !handler || commands_.contains(name))
    {
        throw std::invalid_argument("invalid or duplicate command registration");
    }
    commands_.emplace(std::move(name), std::move(handler));
}

DispatchResult CommandDispatcher::Dispatch(const protocol::Request& request) const
{
    const auto handler = commands_.find(NormalizeName(request.op));
    if (handler == commands_.end())
    {
        return {false, {}, {"UNKNOWN_COMMAND", "unsupported command: " + request.op}};
    }
    return handler->second(request);
}

}
