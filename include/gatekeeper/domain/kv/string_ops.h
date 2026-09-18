#pragma once

#include "gatekeeper/command/dispatcher.h"
#include "gatekeeper/storage/store.h"

namespace gatekeeper::command
{

void RegisterStringOps(Dispatcher& dispatcher, storage::Store& store);

inline void RegisterStringCommands(Dispatcher& dispatcher, storage::Store& store)
{
    RegisterStringOps(dispatcher, store);
}

}
