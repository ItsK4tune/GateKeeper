#include "gatekeeper/command/dispatcher.h"
#include "gatekeeper/storage/memory_store.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

using gatekeeper::command::Dispatcher;

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

void TestIncrDecr()
{
    Dispatcher d;
    auto r1 = d.Dispatch({"1", "INCR", R"({"key":"hits"})"});
    Require(r1.ok, "INCR should succeed");
    Require(r1.result_json == R"({"value":1})", "INCR first value should be 1");

    auto r2 = d.Dispatch({"2", "INCR", R"({"key":"hits"})"});
    Require(r2.ok, "INCR 2nd should succeed");
    Require(r2.result_json == R"({"value":2})", "INCR 2nd value should be 2");

    auto r3 = d.Dispatch({"3", "DECR", R"({"key":"hits"})"});
    Require(r3.ok, "DECR should succeed");
    Require(r3.result_json == R"({"value":1})", "DECR value should be 1");

    auto r4 = d.Dispatch({"4", "INCRBY", R"({"key":"hits","delta":10})"});
    Require(r4.ok, "INCRBY should succeed");
    Require(r4.result_json == R"({"value":11})", "INCRBY value should be 11");

    auto r5 = d.Dispatch({"5", "INCRBY", R"({"key":"hits","delta":-5})"});
    Require(r5.ok, "INCRBY negative should succeed");
    Require(r5.result_json == R"({"value":6})", "INCRBY negative value should be 6");
}

void TestIncrNonInteger()
{
    Dispatcher d;
    d.Dispatch({"1", "SET", R"({"key":"str","value":"hello"})"});
    auto r = d.Dispatch({"2", "INCR", R"({"key":"str"})"});
    Require(!r.ok, "INCR on non-integer should fail");
    Require(r.error.code == "ERR_NOT_AN_INTEGER", "Error code should be ERR_NOT_AN_INTEGER");
}

void TestRateLimit()
{
    Dispatcher d;
    // Limit = 2, window = 200ms
    auto r1 = d.Dispatch({"1", "GK.RATE_LIMIT", R"({"key":"rl:user1","limit":2,"window_ms":200})"});
    Require(r1.ok, "RateLimit 1 should succeed");
    Require(r1.result_json.find(R"("allowed":true)") != std::string::npos, "rl 1 allowed");
    Require(r1.result_json.find(R"("remaining":1)") != std::string::npos, "rl 1 remaining 1");

    auto r2 = d.Dispatch({"2", "GK.RATE_LIMIT", R"({"key":"rl:user1","limit":2,"window_ms":200})"});
    Require(r2.ok, "RateLimit 2 should succeed");
    Require(r2.result_json.find(R"("allowed":true)") != std::string::npos, "rl 2 allowed");
    Require(r2.result_json.find(R"("remaining":0)") != std::string::npos, "rl 2 remaining 0");

    auto r3 = d.Dispatch({"3", "GK.RATE_LIMIT", R"({"key":"rl:user1","limit":2,"window_ms":200})"});
    Require(r3.ok, "RateLimit 3 should return result");
    Require(r3.result_json.find(R"("allowed":false)") != std::string::npos, "rl 3 denied");
    Require(r3.result_json.find(R"("remaining":0)") != std::string::npos, "rl 3 remaining 0");

    // Wait 250ms for window to expire
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    auto r4 = d.Dispatch({"4", "GK.RATE_LIMIT", R"({"key":"rl:user1","limit":2,"window_ms":200})"});
    Require(r4.ok, "RateLimit 4 should succeed after window reset");
    Require(r4.result_json.find(R"("allowed":true)") != std::string::npos, "rl 4 allowed after reset");
}

}

int main()
{
    TestIncrDecr();
    TestIncrNonInteger();
    TestRateLimit();
    std::cout << "All counter and rate limit tests passed!\n";
    return 0;
}
