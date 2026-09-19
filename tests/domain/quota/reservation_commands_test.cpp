#include "gatekeeper/command/dispatcher.h"
#include "gatekeeper/protocol/json_reader.h"
#include "gatekeeper/storage/memory_store.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

using gatekeeper::command::Dispatcher;
using gatekeeper::protocol::JsonReader;

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

std::string ExtractField(const std::string& json, const std::string& target_field)
{
    JsonReader reader(json);
    reader.Expect('{');
    do
    {
        auto field = reader.String();
        reader.Expect(':');
        if (field == target_field)
        {
            return reader.String();
        }
        else
        {
            char p = reader.Peek();
            if (p == '"') reader.String();
            else if (p == 't' || p == 'f') reader.Boolean();
            else reader.SignedNumber();
        }
        if (reader.Take('}')) break;
        reader.Expect(',');
    } while (true);
    return "";
}

void TestReservationCommitFlow()
{
    Dispatcher d;
    // 1. Init quota
    auto r1 = d.Dispatch({"1", "GK.QUOTA_INIT", R"({"key":"quota:user1","amount":1000})"});
    Require(r1.ok, "QuotaInit should succeed");

    // 2. Reserve 300
    auto r2 = d.Dispatch({"2", "GK.RESERVE", R"({"key":"quota:user1","amount":300,"ttl_ms":5000})"});
    Require(r2.ok, "Reserve 300 should succeed");
    Require(r2.result_json.find(R"("remaining":700)") != std::string::npos, "remaining should be 700");

    auto res_id = ExtractField(r2.result_json, "reservation_id");
    Require(!res_id.empty(), "reservation_id should not be empty");

    // 3. Try to reserve 800 (only 700 left) -> should fail
    auto r3 = d.Dispatch({"3", "GK.RESERVE", R"({"key":"quota:user1","amount":800,"ttl_ms":5000})"});
    Require(!r3.ok, "Reserve 800 should fail");
    Require(r3.error.code == "INSUFFICIENT_QUOTA", "Error should be INSUFFICIENT_QUOTA");

    // 4. Commit actual 250 -> refund 50 -> remaining 750
    std::string commit_body = "{\"key\":\"quota:user1\",\"reservation_id\":\"" + res_id + "\",\"actual_amount\":250}";
    auto r4 = d.Dispatch({"4", "GK.COMMIT", commit_body});
    Require(r4.ok, "Commit should succeed");
    Require(r4.result_json.find(R"("refunded":50)") != std::string::npos, "refunded should be 50");
    Require(r4.result_json.find(R"("remaining":750)") != std::string::npos, "remaining should be 750");

    // 5. Check balance
    auto r5 = d.Dispatch({"5", "GK.QUOTA_GET", R"({"key":"quota:user1"})"});
    Require(r5.ok, "QuotaGet should succeed");
    Require(r5.result_json.find(R"("balance":750)") != std::string::npos, "balance should be 750");
}

void TestReservationRollbackFlow()
{
    Dispatcher d;
    d.Dispatch({"1", "GK.QUOTA_INIT", R"({"key":"quota:user2","amount":1000})"});

    auto r1 = d.Dispatch({"2", "GK.RESERVE", R"({"key":"quota:user2","amount":400,"ttl_ms":5000})"});
    Require(r1.ok, "Reserve 400 should succeed");
    auto res_id = ExtractField(r1.result_json, "reservation_id");

    std::string rollback_body = "{\"key\":\"quota:user2\",\"reservation_id\":\"" + res_id + "\"}";
    auto r2 = d.Dispatch({"3", "GK.ROLLBACK", rollback_body});
    Require(r2.ok, "Rollback should succeed");
    Require(r2.result_json.find(R"("refunded":400)") != std::string::npos, "refunded should be 400");
    Require(r2.result_json.find(R"("remaining":1000)") != std::string::npos, "remaining should be 1000");
}

void TestReservationAutoRollbackOnTimeout()
{
    Dispatcher d;
    d.Dispatch({"1", "GK.QUOTA_INIT", R"({"key":"quota:user3","amount":1000})"});

    // Reserve 300 with 100ms TTL
    auto r1 = d.Dispatch({"2", "GK.RESERVE", R"({"key":"quota:user3","amount":300,"ttl_ms":100})"});
    Require(r1.ok, "Reserve 300 should succeed");
    Require(r1.result_json.find(R"("remaining":700)") != std::string::npos, "remaining should be 700");

    // Sleep 150ms for reservation to expire
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // Active purge runs
    const auto purged = d.PurgeExpired(50);
    Require(purged >= 1, "Purge should have reclaimed expired reservation");

    // Verify balance is automatically restored to 1000!
    auto r2 = d.Dispatch({"3", "GK.QUOTA_GET", R"({"key":"quota:user3"})"});
    Require(r2.ok, "QuotaGet should succeed");
    Require(r2.result_json.find(R"("balance":1000)") != std::string::npos, "balance should be auto-restored to 1000");
}

}

int main()
{
    TestReservationCommitFlow();
    TestReservationRollbackFlow();
    TestReservationAutoRollbackOnTimeout();
    std::cout << "All reservation and auto-rollback tests passed!\n";
    return 0;
}
