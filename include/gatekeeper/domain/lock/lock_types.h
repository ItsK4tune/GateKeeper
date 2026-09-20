#pragma once

#include "gatekeeper/storage/entry.h"

namespace gatekeeper::domain::lock
{

using LockRecord = storage::LockRecord;
using LockAcquireResult = storage::LockAcquireResult;
using LockReleaseResult = storage::LockReleaseResult;
using LockExtendResult = storage::LockExtendResult;

}
