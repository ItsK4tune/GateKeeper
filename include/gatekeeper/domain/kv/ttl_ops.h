#pragma once

#include "gatekeeper/command/dispatcher.h"
#include "gatekeeper/storage/store.h"

namespace gatekeeper::command
{

void RegisterTtlOps(Dispatcher& dispatcher, storage::Store& store);

}
