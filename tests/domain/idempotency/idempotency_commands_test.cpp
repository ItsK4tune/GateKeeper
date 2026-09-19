#include "gatekeeper/command/dispatcher.h"
#include "gatekeeper/protocol/gkwp/json_reader.h"
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
            return reader.String();
        }
        else
        {
            char p = reader.Peek();
            if (p == '"')
            {
                reader.String();
            }
            else if (p == '{' || p == '[')
            {
                reader.Skip();
            }
            else
            {
                reader.SignedNumber();
            }
        }
        if (reader.Take('}'))
        {
            break;
        }
        reader.Expect(',');
    } while (true);
    return "";
}

int ExtractIntField(const std::string& json, const std::string& target_field)
{
    JsonReader reader(json);
    reader.Expect('{');
    do
    {
        auto field = reader.String();
        reader.Expect(':');
        if (field == target_field)
        {
            return static_cast<int>(reader.SignedNumber());
        }
        else
        {
            char p = reader.Peek();
            if (p == '"')
            {
                reader.String();
            }
            else if (p == '{' || p == '[')
            {
                reader.Skip();
            }
            else
            {
                reader.SignedNumber();
            }
        }
        if (reader.Take('}'))
        {
            break;
        }
        reader.Expect(',');
    } while (true);
    return 0;
}

void TestIdemBeginCommands()
{
    Dispatcher dispatcher;

    // 1. Initial begin -> EXECUTE
    auto res1 = dispatcher.Dispatch(Request{"1", "GK.IDEM_BEGIN", "{\"key\":\"order-c1\",\"request_hash\":\"h1\",\"ttl_ms\":60000}"});
    Require(res1.ok, "GK.IDEM_BEGIN first call ok");
    Require(ExtractField(res1.result_json, "action") == "EXECUTE", "Action should be EXECUTE");
    auto token = ExtractField(res1.result_json, "owner_token");
    Require(!token.empty(), "owner_token should not be empty");

    // 2. Duplicate while in-progress -> PARK
    auto res2 = dispatcher.Dispatch(Request{"2", "GK.IDEM_BEGIN", "{\"key\":\"order-c1\",\"request_hash\":\"h1\",\"ttl_ms\":60000}"});
    Require(res2.ok, "Duplicate call ok");
    Require(ExtractField(res2.result_json, "action") == "PARK", "Action should be PARK");

    // 3. Hash mismatch -> CONFLICT
    auto res3 = dispatcher.Dispatch(Request{"3", "GK.IDEM_BEGIN", "{\"key\":\"order-c1\",\"request_hash\":\"h2_diff\",\"ttl_ms\":60000}"});
    Require(!res3.ok, "Conflict call should not be ok");
    Require(res3.error.code == "ERR_IDEMPOTENCY_CONFLICT", "Conflict error code");
}

void TestIdemCompleteAndReplayCommands()
{
    Dispatcher dispatcher;

    auto res1 = dispatcher.Dispatch(Request{"1", "GK.IDEM_BEGIN", "{\"key\":\"order-c2\",\"request_hash\":\"h2\",\"ttl_ms\":60000}"});
    auto token = ExtractField(res1.result_json, "owner_token");

    // Complete with valid token
    auto comp = dispatcher.Dispatch(Request{"2", "GK.IDEM_COMPLETE", "{\"key\":\"order-c2\",\"owner_token\":\"" + token + "\",\"response_code\":201,\"response_body\":\"{\\\"status\\\":\\\"created\\\"}\"}"});
    Require(comp.ok, "Complete should be ok");

    // Replay
    auto res2 = dispatcher.Dispatch(Request{"3", "GK.IDEM_BEGIN", "{\"key\":\"order-c2\",\"request_hash\":\"h2\",\"ttl_ms\":60000}"});
    Require(res2.ok, "Replay ok");
    Require(ExtractField(res2.result_json, "action") == "REPLAY", "Action should be REPLAY");
    Require(ExtractIntField(res2.result_json, "response_code") == 201, "Response code 201");
    Require(ExtractField(res2.result_json, "response_body") == "{\"status\":\"created\"}", "Response body matches");

    // Get
    auto get_res = dispatcher.Dispatch(Request{"4", "GK.IDEM_GET", "{\"key\":\"order-c2\"}"});
    Require(get_res.ok, "Get should be ok");
    Require(ExtractField(get_res.result_json, "status") == "COMPLETED", "Status should be COMPLETED");
}

void TestIdemFailCommands()
{
    Dispatcher dispatcher;

    auto res1 = dispatcher.Dispatch(Request{"1", "GK.IDEM_BEGIN", "{\"key\":\"order-c3\",\"request_hash\":\"h3\",\"ttl_ms\":60000}"});
    auto token = ExtractField(res1.result_json, "owner_token");

    // Wrong token
    auto fail_bad = dispatcher.Dispatch(Request{"2", "GK.IDEM_FAIL", "{\"key\":\"order-c3\",\"owner_token\":\"wrong\",\"error_message\":\"err\"}"});
    Require(!fail_bad.ok, "Fail with bad token should fail");
    Require(fail_bad.error.code == "ERR_TOKEN_MISMATCH", "Token mismatch code");

    // Valid fail
    auto fail_ok = dispatcher.Dispatch(Request{"3", "GK.IDEM_FAIL", "{\"key\":\"order-c3\",\"owner_token\":\"" + token + "\",\"error_message\":\"payment declined\"}"});
    Require(fail_ok.ok, "Fail ok");

    auto get_res = dispatcher.Dispatch(Request{"4", "GK.IDEM_GET", "{\"key\":\"order-c3\"}"});
    Require(get_res.ok, "Get ok");
    Require(ExtractField(get_res.result_json, "status") == "FAILED", "Status should be FAILED");
    Require(ExtractField(get_res.result_json, "response_body") == "payment declined", "Error message saved");
}

void TestIdemInvalidArguments()
{
    Dispatcher dispatcher;

    auto res1 = dispatcher.Dispatch(Request{"1", "GK.IDEM_BEGIN", "{}"});
    Require(!res1.ok, "Missing key/hash should fail");
    Require(res1.error.code == "INVALID_ARGUMENTS", "Error code INVALID_ARGUMENTS");

    auto res2 = dispatcher.Dispatch(Request{"2", "GK.IDEM_COMPLETE", "{\"key\":\"k\"}"});
    Require(!res2.ok, "Missing token should fail");

    auto res3 = dispatcher.Dispatch(Request{"3", "GK.IDEM_FAIL", "{\"key\":\"k\"}"});
    Require(!res3.ok, "Missing token should fail");
}

}

int main()
{
    TestIdemBeginCommands();
    TestIdemCompleteAndReplayCommands();
    TestIdemFailCommands();
    TestIdemInvalidArguments();

    std::cout << "All idempotency command tests passed!\n";
    return 0;
}
