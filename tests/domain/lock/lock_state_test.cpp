#include "gatekeeper/storage/memory_store.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

using gatekeeper::storage::MemoryStore;
using gatekeeper::storage::LockRecord;

namespace
{

void Require(bool ok, const char* msg)
{
    if (!ok)
    {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

void TestDirectStorageLockAcquireRelease()
{
    MemoryStore store;

    // 1. Acquire
    auto acq1 = store.LockAcquire("res:order:1", 50, "token1");
    Require(acq1.ok, "acq1.ok");
    Require(acq1.acquired, "acq1.acquired");
    Require(acq1.owner_token == "token1", "owner_token matches");
    Require(acq1.fencing_token > 0, "fencing_token > 0");

    // 2. Conflict
    auto acq2 = store.LockAcquire("res:order:1", 50, "token2");
    Require(acq2.ok, "acq2.ok");
    Require(!acq2.acquired, "acq2 should not be acquired");
    Require(acq2.error_code == "LOCK_HELD", "acq2 error_code LOCK_HELD");

    // 3. LockGet
    auto get1 = store.LockGet("res:order:1");
    Require(get1.has_value(), "get1 has value");
    Require(get1->owner_token == "token1", "get1 token matches");

    // 4. Release wrong token
    auto rel_bad = store.LockRelease("res:order:1", "wrong");
    Require(!rel_bad.ok, "rel_bad fails");
    Require(rel_bad.error_code == "ERR_LOCK_TOKEN_MISMATCH", "mismatch code");

    // 5. Release correct token
    auto rel_ok = store.LockRelease("res:order:1", "token1");
    Require(rel_ok.ok, "rel_ok succeeds");
    Require(rel_ok.released, "rel_ok released");

    // 6. LockGet after release
    auto get2 = store.LockGet("res:order:1");
    Require(!get2.has_value(), "get2 should be nullopt after release");
}

void TestLockExpirationAndPurge()
{
    MemoryStore store;

    auto acq = store.LockAcquire("res:temp:1", 20, "token_temp");
    Require(acq.ok && acq.acquired, "temp acquire ok");

    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    // Lazy expiration on Get
    auto get = store.LockGet("res:temp:1");
    Require(!get.has_value(), "Expired lock should not be returned by LockGet");

    // Lazy expiration on Acquire
    auto acq_after = store.LockAcquire("res:temp:1", 50, "new_token");
    Require(acq_after.ok && acq_after.acquired, "Can acquire expired lock");

    // Purge
    auto acq_purge = store.LockAcquire("res:purge:1", 10, "tok_purge");
    Require(acq_purge.ok, "acq_purge ok");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    std::size_t purged = store.PurgeExpired(50);
    Require(purged >= 1, "PurgeExpired should purge expired lock");
}

void TestLockExtend()
{
    MemoryStore store;

    auto acq = store.LockAcquire("res:extend:1", 30, "tok");
    Require(acq.ok && acq.acquired, "acquire ok");

    auto ext = store.LockExtend("res:extend:1", "tok", 100);
    Require(ext.ok && ext.extended, "extend ok");
    Require(ext.ttl_remaining_ms == 100, "ttl updated");

    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    auto get = store.LockGet("res:extend:1");
    Require(get.has_value(), "lock should still exist because TTL was extended");
}

}

int main()
{
    TestDirectStorageLockAcquireRelease();
    TestLockExpirationAndPurge();
    TestLockExtend();
    std::cout << "All lock state tests passed!\n";
    return 0;
}
