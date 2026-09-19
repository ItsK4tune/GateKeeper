#include "gatekeeper/storage/memory_store.h"
#include "gatekeeper/domain/idempotency/idempotency_types.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

using gatekeeper::storage::MemoryStore;
namespace idem = gatekeeper::domain::idempotency;

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

void TestIdemBeginFirstCall()
{
    MemoryStore store;
    auto res = store.IdemBegin("order-101", "hash_abc", 60000, "");
    Require(res.ok, "IdemBegin first call should be ok");
    Require(res.action == idem::Action::Execute, "IdemBegin first call action should be Execute");
    Require(!res.owner_token.empty(), "IdemBegin owner token should not be empty");

    auto rec = store.IdemGet("order-101");
    Require(rec.has_value(), "Record should exist");
    Require(rec->status == idem::Status::InProgress, "Status should be InProgress");
    Require(rec->owner_token == res.owner_token, "Owner token should match");
    Require(rec->request_hash == "hash_abc", "Hash should match");
}

void TestIdemBeginDuplicateInProgress()
{
    MemoryStore store;
    auto res1 = store.IdemBegin("order-102", "hash_abc", 60000, "");
    Require(res1.ok && res1.action == idem::Action::Execute, "First call execute");

    auto res2 = store.IdemBegin("order-102", "hash_abc", 60000, "");
    Require(res2.ok, "Second call ok");
    Require(res2.action == idem::Action::Park, "Second call action should be Park");
}

void TestIdemBeginSameOwnerRetry()
{
    MemoryStore store;
    auto res1 = store.IdemBegin("order-103", "hash_abc", 60000, "token_123");
    Require(res1.ok && res1.action == idem::Action::Execute, "First call execute");
    Require(res1.owner_token == "token_123", "Token matches specified");

    auto res2 = store.IdemBegin("order-103", "hash_abc", 60000, "token_123");
    Require(res2.ok, "Same owner retry ok");
    Require(res2.action == idem::Action::Execute, "Same owner retry action should be Execute");
}

void TestIdemBeginHashMismatchConflict()
{
    MemoryStore store;
    auto res1 = store.IdemBegin("order-104", "hash_orig", 60000, "");
    Require(res1.ok && res1.action == idem::Action::Execute, "First call execute");

    auto res2 = store.IdemBegin("order-104", "hash_different", 60000, "");
    Require(!res2.ok, "Hash mismatch not ok");
    Require(res2.action == idem::Action::Conflict, "Hash mismatch action should be Conflict");
    Require(res2.error_code == "ERR_IDEMPOTENCY_CONFLICT", "Conflict error code");
}

void TestIdemCompleteAndReplay()
{
    MemoryStore store;
    auto res1 = store.IdemBegin("order-105", "hash_abc", 60000, "");
    Require(res1.ok && res1.action == idem::Action::Execute, "Begin execute");

    auto comp = store.IdemComplete("order-105", res1.owner_token, 201, "{\"status\":\"created\"}");
    Require(comp.ok, "Complete ok");
    Require(comp.completed, "Complete completed flag");

    auto rec = store.IdemGet("order-105");
    Require(rec.has_value(), "Record exists");
    Require(rec->status == idem::Status::Completed, "Status is Completed");
    Require(rec->response_code == 201, "Response code matches");
    Require(rec->response_body == "{\"status\":\"created\"}", "Response body matches");

    auto res2 = store.IdemBegin("order-105", "hash_abc", 60000, "");
    Require(res2.ok, "Replay call ok");
    Require(res2.action == idem::Action::Replay, "Replay action");
    Require(res2.cached_code == 201, "Cached code matches");
    Require(res2.cached_response == "{\"status\":\"created\"}", "Cached response matches");
}

void TestIdemCompleteInvalidToken()
{
    MemoryStore store;
    auto res1 = store.IdemBegin("order-106", "hash_abc", 60000, "valid_token");
    Require(res1.ok, "Begin ok");

    auto comp = store.IdemComplete("order-106", "wrong_token", 200, "ok");
    Require(!comp.ok, "Complete with wrong token should fail");
    Require(comp.error_code == "ERR_TOKEN_MISMATCH", "Error code should be ERR_TOKEN_MISMATCH");
}

void TestIdemFailAndRetry()
{
    MemoryStore store;
    auto res1 = store.IdemBegin("order-107", "hash_fail", 60000, "tok1");
    Require(res1.ok && res1.action == idem::Action::Execute, "Begin ok");

    auto fail = store.IdemFail("order-107", "tok1", "third party timeout");
    Require(fail.ok && fail.failed, "Fail ok");

    auto rec = store.IdemGet("order-107");
    Require(rec.has_value(), "Record exists");
    Require(rec->status == idem::Status::Failed, "Status is Failed");
    Require(rec->response_body == "third party timeout", "Error message saved");

    auto res2 = store.IdemBegin("order-107", "hash_fail", 60000, "tok2");
    Require(res2.ok, "Retry after fail ok");
    Require(res2.action == idem::Action::Execute, "Retry after fail action is Execute");
}

void TestIdemExpirationAndPurge()
{
    MemoryStore store;
    auto res = store.IdemBegin("order-exp", "hash_exp", 50, "");
    Require(res.ok, "Begin ok");

    std::this_thread::sleep_for(std::chrono::milliseconds(70));

    auto rec = store.IdemGet("order-exp");
    Require(!rec.has_value(), "Record should be expired and pruned on get");

    store.IdemBegin("order-exp2", "hash_exp2", 50, "");
    std::this_thread::sleep_for(std::chrono::milliseconds(70));
    auto purged = store.PurgeExpired(10);
    Require(purged >= 1, "Purge should have removed at least 1 record");
}

}

int main()
{
    TestIdemBeginFirstCall();
    TestIdemBeginDuplicateInProgress();
    TestIdemBeginSameOwnerRetry();
    TestIdemBeginHashMismatchConflict();
    TestIdemCompleteAndReplay();
    TestIdemCompleteInvalidToken();
    TestIdemFailAndRetry();
    TestIdemExpirationAndPurge();

    std::cout << "All idempotency state tests passed!\n";
    return 0;
}
