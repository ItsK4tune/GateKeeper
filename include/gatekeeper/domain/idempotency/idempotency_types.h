#pragma once

#include "gatekeeper/storage/entry.h"

namespace gatekeeper::domain::idempotency
{

using Status = storage::IdempotencyStatus;
using Record = storage::IdempotencyRecord;
using Action = storage::IdempotencyAction;
using BeginResult = storage::IdempotencyBeginResult;
using CompleteResult = storage::IdempotencyCompleteResult;
using FailResult = storage::IdempotencyFailResult;

}