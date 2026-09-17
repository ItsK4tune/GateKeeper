#pragma once

namespace gatekeeper::storage
{
class StringStore;
}

namespace gatekeeper::command
{
class CommandDispatcher;
void RegisterStringCommands(CommandDispatcher& dispatcher, storage::StringStore& store);
}
