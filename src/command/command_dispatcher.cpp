#include "gatekeeper/command/command_dispatcher.h"
#include "gatekeeper/command/name.h"
#include "gatekeeper/command/string_commands.h"
#include "gatekeeper/storage/in_memory_string_store.h"

#include <stdexcept>
#include <utility>

namespace gatekeeper::command
{

CommandDispatcher::CommandDispatcher()
    : CommandDispatcher(std::make_unique<storage::InMemoryStringStore>())
{
}

CommandDispatcher::CommandDispatcher(std::unique_ptr<storage::StringStore> store)
    : store_(std::move(store))
{
    if (!store_)
    {
        throw std::invalid_argument("string store is required");
    }
    Register("PING", [](const protocol::Request&) {
        return DispatchResult{true, R"({"pong":true})", {}};
    });
    RegisterStringCommands(*this, *store_);
}

CommandDispatcher::~CommandDispatcher() = default;

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
