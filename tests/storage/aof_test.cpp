#include "gatekeeper/storage/aof/aof_writer.h"
#include "gatekeeper/storage/aof/aof_loader.h"
#include "gatekeeper/command/dispatcher.h"
#include "gatekeeper/protocol/gkwp/json_reader.h"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>

using gatekeeper::command::Dispatcher;
using gatekeeper::storage::aof::AofWriter;
using gatekeeper::storage::aof::AofLoader;
using gatekeeper::storage::aof::FsyncPolicy;

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

void TestAofWriterAndLoader()
{
    const std::string test_dir = "/tmp/gk_aof_test_1";
    std::filesystem::remove_all(test_dir);
    const std::string aof_file = test_dir + "/test.aof";

    {
        AofWriter writer(aof_file, FsyncPolicy::Always);
        Require(writer.IsOpen(), "AofWriter should be open");

        writer.Append("SET", "{\"key\":\"k1\",\"value\":\"val1\"}");
        writer.Append("GK.IDEM_BEGIN", "{\"key\":\"order_aof_1\",\"request_hash\":\"hash_1\",\"ttl_ms\":60000,\"owner_token\":\"tok_1\"}");
        writer.Append("GK.IDEM_COMPLETE", "{\"key\":\"order_aof_1\",\"owner_token\":\"tok_1\",\"response_code\":200,\"response_body\":\"{\\\"done\\\":true}\"}");
        writer.Close();
    }

    Require(std::filesystem::exists(aof_file), "AOF file should exist");

    // Load into a new dispatcher
    Dispatcher dispatcher;
    auto load_res = AofLoader::Load(aof_file, dispatcher);
    Require(load_res.ok, "AofLoader should succeed");
    Require(load_res.lines_replayed == 3, "All 3 lines should be replayed");

    // Verify state
    auto val = dispatcher.GetStore().Get("k1");
    Require(val.has_value() && *val == "val1", "Key k1 value should be val1");

    auto rec = dispatcher.GetStore().IdemGet("order_aof_1");
    Require(rec.has_value(), "Idempotency record should exist");
    Require(rec->response_code == 200, "Response code should be 200");
    Require(rec->response_body == "{\"done\":true}", "Response body should match");

    std::filesystem::remove_all(test_dir);
}

void TestAofTruncatedLineRecovery()
{
    const std::string test_dir = "/tmp/gk_aof_test_2";
    std::filesystem::remove_all(test_dir);
    const std::string aof_file = test_dir + "/crash.aof";

    {
        AofWriter writer(aof_file, FsyncPolicy::Always);
        writer.Append("SET", "{\"key\":\"good_key\",\"value\":\"good_val\"}");
        writer.Close();
    }

    // Append truncated half-line simulating power loss mid-write
    {
        std::ofstream raw(aof_file, std::ios::app);
        raw << "SET {\"key\":\"partial_";
    }

    Dispatcher dispatcher;
    auto load_res = AofLoader::Load(aof_file, dispatcher);
    Require(load_res.ok, "Loader should survive truncated line");
    Require(load_res.lines_replayed == 1, "Good line should be replayed");
    Require(load_res.lines_skipped >= 1, "Truncated line should be skipped");

    auto val = dispatcher.GetStore().Get("good_key");
    Require(val.has_value() && *val == "good_val", "good_key should exist");

    std::filesystem::remove_all(test_dir);
}

void TestDispatcherAutoAofAppend()
{
    const std::string test_dir = "/tmp/gk_aof_test_3";
    std::filesystem::remove_all(test_dir);
    const std::string aof_file = test_dir + "/auto.aof";

    auto writer = std::make_shared<AofWriter>(aof_file, FsyncPolicy::Always);
    {
        Dispatcher dispatcher;
        dispatcher.SetAofWriter(writer);

        dispatcher.Dispatch(gatekeeper::protocol::Request{"1", "SET", "{\"key\":\"auto_k\",\"value\":\"auto_v\"}"});
        dispatcher.Dispatch(gatekeeper::protocol::Request{"2", "GK.IDEM_BEGIN", "{\"key\":\"auto_idem\",\"request_hash\":\"h_auto\",\"ttl_ms\":60000}"});
    }
    writer->Close();

    // Replay with new dispatcher
    Dispatcher fresh_dispatcher;
    auto load_res = AofLoader::Load(aof_file, fresh_dispatcher);
    Require(load_res.lines_replayed == 2, "2 auto-appended lines should be replayed");

    auto val = fresh_dispatcher.GetStore().Get("auto_k");
    Require(val.has_value() && *val == "auto_v", "auto_k should exist");

    std::filesystem::remove_all(test_dir);
}

}

int main()
{
    TestAofWriterAndLoader();
    TestAofTruncatedLineRecovery();
    TestDispatcherAutoAofAppend();

    std::cout << "All AOF persistence tests passed!\n";
    return 0;
}
