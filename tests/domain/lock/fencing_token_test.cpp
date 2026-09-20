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

std::uint64_t ExtractUintField(const std::string& json, const std::string& target_field)
{
    auto val = ExtractField(json, target_field);
    return val.empty() ? 0 : std::stoull(val);
}

void TestStrictMonotonicFencingTokens()
{
    Dispatcher dispatcher;

    // 1. Acquire resource A
    auto res1 = dispatcher.Dispatch(Request{"1", "GK.LOCK_ACQUIRE", "{\"resource\":\"fencing:res:1\",\"ttl_ms\":60000}"});
    Require(res1.ok, "res1 ok");
    auto token1 = ExtractField(res1.result_json, "owner_token");
    auto f1 = ExtractUintField(res1.result_json, "fencing_token");
    Require(f1 > 0, "fencing token 1 > 0");

    // 2. Extend keeps same fencing token
    auto ext = dispatcher.Dispatch(Request{"2", "GK.LOCK_EXTEND", "{\"resource\":\"fencing:res:1\",\"owner_token\":\"" + token1 + "\",\"ttl_ms\":60000}"});
    Require(ext.ok, "ext ok");
    auto f_ext = ExtractUintField(ext.result_json, "fencing_token");
    Require(f_ext == f1, "Fencing token must remain identical during lock extension");

    // 3. Release resource A
    auto rel1 = dispatcher.Dispatch(Request{"3", "GK.LOCK_RELEASE", "{\"resource\":\"fencing:res:1\",\"owner_token\":\"" + token1 + "\"}"});
    Require(rel1.ok, "rel1 ok");

    // 4. Acquire resource A again -> strictly greater fencing token
    auto res2 = dispatcher.Dispatch(Request{"4", "GK.LOCK_ACQUIRE", "{\"resource\":\"fencing:res:1\",\"ttl_ms\":60000}"});
    Require(res2.ok, "res2 ok");
    auto f2 = ExtractUintField(res2.result_json, "fencing_token");
    Require(f2 > f1, "Subsequent acquire must produce strictly greater fencing token (Martin Kleppmann compliance)");

    // 5. Acquire resource B -> strictly greater fencing token across resources
    auto res3 = dispatcher.Dispatch(Request{"5", "GK.LOCK_ACQUIRE", "{\"resource\":\"fencing:res:2\",\"ttl_ms\":60000}"});
    Require(res3.ok, "res3 ok");
    auto f3 = ExtractUintField(res3.result_json, "fencing_token");
    Require(f3 > f2, "Global fencing token sequence must be strictly increasing");
}

void TestStaleOwnerCannotDeleteNewOwnerLock()
{
    Dispatcher dispatcher;

    // 1. Owner 1 acquires lock with short TTL (30ms)
    auto acq1 = dispatcher.Dispatch(Request{"1", "GK.LOCK_ACQUIRE", "{\"resource\":\"stale:res:1\",\"ttl_ms\":30}"});
    Require(acq1.ok, "acq1 ok");
    auto tok1 = ExtractField(acq1.result_json, "owner_token");

    // 2. Owner 1 pauses (sleep 50ms, simulating GC pause or network delay)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // 3. Lock expired, Owner 2 acquires lock
    auto acq2 = dispatcher.Dispatch(Request{"2", "GK.LOCK_ACQUIRE", "{\"resource\":\"stale:res:1\",\"ttl_ms\":60000}"});
    Require(acq2.ok && ExtractField(acq2.result_json, "acquired") == "true", "Owner 2 acquired lock");
    auto tok2 = ExtractField(acq2.result_json, "owner_token");
    Require(tok1 != tok2, "New owner token must differ from stale owner token");

    // 4. Owner 1 wakes up and attempts to release the lock -> MUST BE REJECTED
    auto stale_rel = dispatcher.Dispatch(Request{"3", "GK.LOCK_RELEASE", "{\"resource\":\"stale:res:1\",\"owner_token\":\"" + tok1 + "\"}"});
    Require(!stale_rel.ok, "Stale owner must NOT be allowed to release lock of new owner");
    Require(stale_rel.error.code == "ERR_LOCK_TOKEN_MISMATCH", "Error code ERR_LOCK_TOKEN_MISMATCH");

    // 5. Owner 2 still owns the lock
    auto get = dispatcher.Dispatch(Request{"4", "GK.LOCK_GET", "{\"resource\":\"stale:res:1\"}"});
    Require(get.ok, "get ok");
    Require(ExtractField(get.result_json, "owner_token") == tok2, "Owner 2 still holds the lock");
}

}

int main()
{
    TestStrictMonotonicFencingTokens();
    TestStaleOwnerCannotDeleteNewOwnerLock();
    std::cout << "All fencing token tests passed!\n";
    return 0;
}
