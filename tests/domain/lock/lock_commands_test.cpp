#include "gatekeeper/command/dispatcher.h"
#include "gatekeeper/protocol/json_reader.h"
#include "gatekeeper/storage/memory_store.h"

#include <cassert>
#include <iostream>
#include <string>

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

void TestLockAcquireAndRelease()
{
    Dispatcher dispatcher;

    // 1. Initial acquire -> ok, acquired = true
    auto res1 = dispatcher.Dispatch(Request{"1", "GK.LOCK_ACQUIRE", "{\"resource\":\"order:1001\",\"ttl_ms\":60000}"});
    Require(res1.ok, "First acquire ok");
    Require(ExtractField(res1.result_json, "acquired") == "true", "Acquired should be true");
    auto token = ExtractField(res1.result_json, "owner_token");
    Require(!token.empty(), "owner_token should not be empty");

    // 2. Second acquire on same resource -> acquired = false
    auto res2 = dispatcher.Dispatch(Request{"2", "GK.LOCK_ACQUIRE", "{\"resource\":\"order:1001\",\"ttl_ms\":60000}"});
    Require(res2.ok, "Second acquire returns ok response with acquired=false");
    Require(ExtractField(res2.result_json, "acquired") == "false", "Acquired should be false");

    // 3. Release with wrong token -> fails
    auto rel_wrong = dispatcher.Dispatch(Request{"3", "GK.LOCK_RELEASE", "{\"resource\":\"order:1001\",\"owner_token\":\"bad_token\"}"});
    Require(!rel_wrong.ok, "Release with wrong token fails");
    Require(rel_wrong.error.code == "ERR_LOCK_TOKEN_MISMATCH", "Token mismatch error code");

    // 4. Release with correct token -> ok
    auto rel_ok = dispatcher.Dispatch(Request{"4", "GK.LOCK_RELEASE", "{\"resource\":\"order:1001\",\"owner_token\":\"" + token + "\"}"});
    Require(rel_ok.ok, "Release with correct token ok");
    Require(ExtractField(rel_ok.result_json, "released") == "true", "Released should be true");

    // 5. Acquire again after release -> ok
    auto res3 = dispatcher.Dispatch(Request{"5", "GK.LOCK_ACQUIRE", "{\"resource\":\"order:1001\",\"ttl_ms\":60000}"});
    Require(res3.ok, "Acquire after release ok");
    Require(ExtractField(res3.result_json, "acquired") == "true", "Acquired should be true again");
}

void TestLockExtend()
{
    Dispatcher dispatcher;

    auto res1 = dispatcher.Dispatch(Request{"1", "GK.LOCK_ACQUIRE", "{\"resource\":\"invoice:2002\",\"ttl_ms\":10000}"});
    Require(res1.ok, "Acquire ok");
    auto token = ExtractField(res1.result_json, "owner_token");

    // Extend with wrong token -> fails
    auto ext_wrong = dispatcher.Dispatch(Request{"2", "GK.LOCK_EXTEND", "{\"resource\":\"invoice:2002\",\"owner_token\":\"invalid\",\"ttl_ms\":30000}"});
    Require(!ext_wrong.ok, "Extend with wrong token fails");

    // Extend with correct token -> ok
    auto ext_ok = dispatcher.Dispatch(Request{"3", "GK.LOCK_EXTEND", "{\"resource\":\"invoice:2002\",\"owner_token\":\"" + token + "\",\"ttl_ms\":30000}"});
    Require(ext_ok.ok, "Extend ok");
    Require(ExtractField(ext_ok.result_json, "extended") == "true", "Extended should be true");

    // Get
    auto get_res = dispatcher.Dispatch(Request{"4", "GK.LOCK_GET", "{\"resource\":\"invoice:2002\"}"});
    Require(get_res.ok, "Lock get ok");
    Require(ExtractField(get_res.result_json, "owner_token") == token, "Owner token matches");
}

void TestLockInvalidArgs()
{
    Dispatcher dispatcher;

    auto res1 = dispatcher.Dispatch(Request{"1", "GK.LOCK_ACQUIRE", "{}"});
    Require(!res1.ok, "Empty body fails");
    Require(res1.error.code == "INVALID_ARGUMENTS", "Error code INVALID_ARGUMENTS");

    auto res2 = dispatcher.Dispatch(Request{"2", "GK.LOCK_RELEASE", "{\"resource\":\"r\"}"});
    Require(!res2.ok, "Missing owner token fails");

    auto res3 = dispatcher.Dispatch(Request{"3", "GK.LOCK_EXTEND", "{\"resource\":\"r\"}"});
    Require(!res3.ok, "Missing owner token fails");
}

}

int main()
{
    TestLockAcquireAndRelease();
    TestLockExtend();
    TestLockInvalidArgs();
    std::cout << "All lock command tests passed!\n";
    return 0;
}
