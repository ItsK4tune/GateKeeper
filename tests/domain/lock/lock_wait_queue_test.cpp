#include "gatekeeper/command/dispatcher.h"
#include "gatekeeper/domain/lock/lock_wait_queue.h"
#include "gatekeeper/protocol/json_reader.h"
#include "gatekeeper/storage/memory_store.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using gatekeeper::command::Dispatcher;
using gatekeeper::domain::lock::GetGlobalLockWaitQueue;
using gatekeeper::protocol::JsonReader;
using gatekeeper::protocol::Request;

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
            char p = reader.Peek();
            if (p == '"') return reader.String();
            if (p == 't' || p == 'f') return reader.Boolean() ? "true" : "false";
            return std::to_string(reader.SignedNumber());
        }
        else
        {
            char p = reader.Peek();
            if (p == '"') reader.String();
            else if (p == '{' || p == '[') reader.Skip();
            else if (p == 't' || p == 'f') reader.Boolean();
            else reader.SignedNumber();
        }
        if (reader.Take('}')) break;
        reader.Expect(',');
    } while (true);
    return "";
}

void TestFifoOrder()
{
    Dispatcher dispatcher;

    // Worker 1 acquires lock
    auto res1 = dispatcher.Dispatch(Request{"1", "GK.LOCK_ACQUIRE", "{\"resource\":\"fifo:res:1\",\"ttl_ms\":60000}"});
    Require(res1.ok, "Worker 1 acquire ok");
    auto tok1 = ExtractField(res1.result_json, "owner_token");

    // Worker 2 waits (in background thread)
    std::string tok2;
    std::thread t2([&]() {
        auto r = dispatcher.Dispatch(Request{"2", "GK.LOCK_WAIT", "{\"resource\":\"fifo:res:1\",\"ttl_ms\":60000,\"max_wait_ms\":2000}"});
        Require(r.ok, "Worker 2 wait ok");
        tok2 = ExtractField(r.result_json, "owner_token");
    });

    // Worker 3 waits shortly after (in background thread)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::string tok3;
    std::thread t3([&]() {
        auto r = dispatcher.Dispatch(Request{"3", "GK.LOCK_WAIT", "{\"resource\":\"fifo:res:1\",\"ttl_ms\":60000,\"max_wait_ms\":2000}"});
        Require(r.ok, "Worker 3 wait ok");
        tok3 = ExtractField(r.result_json, "owner_token");
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Release worker 1 -> worker 2 MUST be woken and granted lock first
    auto rel1 = dispatcher.Dispatch(Request{"4", "GK.LOCK_RELEASE", "{\"resource\":\"fifo:res:1\",\"owner_token\":\"" + tok1 + "\"}"});
    Require(rel1.ok, "Release 1 ok");

    t2.join();
    Require(!tok2.empty(), "Worker 2 received lock");

    // Release worker 2 -> worker 3 MUST be woken and granted lock next
    auto rel2 = dispatcher.Dispatch(Request{"5", "GK.LOCK_RELEASE", "{\"resource\":\"fifo:res:1\",\"owner_token\":\"" + tok2 + "\"}"});
    Require(rel2.ok, "Release 2 ok");

    t3.join();
    Require(!tok3.empty(), "Worker 3 received lock");
}

void TestWaitTimeout()
{
    Dispatcher dispatcher;

    // Worker holds lock
    auto res1 = dispatcher.Dispatch(Request{"1", "GK.LOCK_ACQUIRE", "{\"resource\":\"timeout:res:1\",\"ttl_ms\":60000}"});
    Require(res1.ok, "Acquire ok");

    // Worker 2 waits with 50ms timeout, but lock is not released
    auto res2 = dispatcher.Dispatch(Request{"2", "GK.LOCK_WAIT", "{\"resource\":\"timeout:res:1\",\"ttl_ms\":60000,\"max_wait_ms\":50}"});
    Require(!res2.ok, "Wait should time out");
    Require(res2.error.code == "ERR_LOCK_WAIT_TIMEOUT", "Timeout error code matches");
}

}

int main()
{
    TestFifoOrder();
    TestWaitTimeout();
    std::cout << "All lock wait queue tests passed!\n";
    return 0;
}
