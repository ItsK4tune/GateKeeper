#include "gatekeeper/domain/idempotency/parking_lot.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using gatekeeper::domain::idempotency::ParkingLot;

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

void TestParkAndBroadcast()
{
    ParkingLot lot;
    std::vector<std::pair<int, std::string>> results;

    lot.Park("order-1", 101, [&](int code, std::string_view body, bool timed_out) {
        Require(!timed_out, "Should not be timed out");
        results.emplace_back(code, std::string(body));
    });

    lot.Park("order-1", 102, [&](int code, std::string_view body, bool timed_out) {
        Require(!timed_out, "Should not be timed out");
        results.emplace_back(code, std::string(body));
    });

    lot.Park("order-1", 103, [&](int code, std::string_view body, bool timed_out) {
        Require(!timed_out, "Should not be timed out");
        results.emplace_back(code, std::string(body));
    });

    Require(lot.WaiterCount("order-1") == 3, "WaiterCount should be 3");
    Require(lot.TotalWaiters() == 3, "TotalWaiters should be 3");

    auto unparked = lot.BroadcastAndUnpark("order-1", 200, "{\"status\":\"success\"}");
    Require(unparked == 3, "Unparked count should be 3");
    Require(results.size() == 3, "Results size should be 3");
    for (const auto& r : results)
    {
        Require(r.first == 200, "Result code should be 200");
        Require(r.second == "{\"status\":\"success\"}", "Result body should match");
    }

    Require(lot.WaiterCount("order-1") == 0, "WaiterCount after broadcast should be 0");
    Require(lot.TotalWaiters() == 0, "TotalWaiters after broadcast should be 0");
}

void TestCancelWaiter()
{
    ParkingLot lot;
    int called_id = 0;

    lot.Park("order-2", 201, [&](int, std::string_view, bool) {
        called_id = 201;
    });

    lot.Park("order-2", 202, [&](int, std::string_view, bool) {
        called_id = 202;
    });

    Require(lot.Cancel("order-2", 201), "Cancel 201 should return true");
    Require(!lot.Cancel("order-2", 201), "Cancel 201 again should return false");
    Require(lot.WaiterCount("order-2") == 1, "WaiterCount should be 1");

    lot.BroadcastAndUnpark("order-2", 200, "ok");
    Require(called_id == 202, "Only waiter 202 should have been called");
}

void TestPurgeTimeout()
{
    ParkingLot lot;
    bool waiter1_timed_out = false;
    bool waiter2_called = false;

    lot.Park("order-3", 301, [&](int code, std::string_view, bool timed_out) {
        if (timed_out && code == 504)
        {
            waiter1_timed_out = true;
        }
    }, 1000);

    lot.Park("order-3", 302, [&](int, std::string_view, bool) {
        waiter2_called = true;
    }, 5000);

    auto purged_early = lot.PurgeTimeout(500);
    Require(purged_early == 0, "No waiters should be purged at 500ms");
    Require(!waiter1_timed_out, "Waiter 1 should not have timed out yet");

    auto purged_late = lot.PurgeTimeout(1500);
    Require(purged_late == 1, "1 waiter should be purged at 1500ms");
    Require(waiter1_timed_out, "Waiter 1 should have timed out");
    Require(!waiter2_called, "Waiter 2 should still be parked");
    Require(lot.WaiterCount("order-3") == 1, "Waiter count should be 1");
}

void TestValidationAndEdgeCases()
{
    ParkingLot lot;

    Require(!lot.Park("", 1, [](int, std::string_view, bool) {}), "Empty key should fail");
    Require(!lot.Park("k", 0, [](int, std::string_view, bool) {}), "Zero waiter id should fail");
    Require(!lot.Park("k", 1, nullptr), "Null callback should fail");

    Require(lot.Park("k", 1, [](int, std::string_view, bool) {}), "Valid park");
    Require(!lot.Park("k", 1, [](int, std::string_view, bool) {}), "Duplicate waiter id should fail");

    Require(lot.BroadcastAndUnpark("nonexistent", 200, "") == 0, "Broadcast nonexistent returns 0");
    Require(!lot.Cancel("nonexistent", 1), "Cancel nonexistent returns false");
}

}

int main()
{
    TestParkAndBroadcast();
    TestCancelWaiter();
    TestPurgeTimeout();
    TestValidationAndEdgeCases();

    std::cout << "All parking lot tests passed!\n";
    return 0;
}
