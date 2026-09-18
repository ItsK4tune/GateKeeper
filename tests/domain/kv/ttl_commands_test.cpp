#include "gatekeeper/command/dispatcher.h"
#include "gatekeeper/storage/memory_store.h"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

using gatekeeper::command::Dispatcher;

namespace
{

void Require(bool ok, const char* message)
{
    if (!ok)
    {
        throw std::runtime_error(message);
    }
}

}

int main()
{
    try
    {
        auto store = std::make_unique<gatekeeper::storage::MemoryStore>();
        auto* store_ptr = store.get();
        Dispatcher d(std::move(store));

        auto set = [&](const std::string& body) {
            return d.Dispatch({"1", "SET", body});
        };
        auto get = [&](const std::string& key) {
            return d.Dispatch({"2", "GET", "{\"key\":\"" + key + "\"}"});
        };
        auto expire = [&](const std::string& body) {
            return d.Dispatch({"3", "EXPIRE", body});
        };
        auto pexpire = [&](const std::string& body) {
            return d.Dispatch({"4", "PEXPIRE", body});
        };
        auto ttl = [&](const std::string& key) {
            return d.Dispatch({"5", "TTL", "{\"key\":\"" + key + "\"}"});
        };
        auto pttl = [&](const std::string& key) {
            return d.Dispatch({"6", "PTTL", "{\"key\":\"" + key + "\"}"});
        };
        auto persist = [&](const std::string& key) {
            return d.Dispatch({"7", "PERSIST", "{\"key\":\"" + key + "\"}"});
        };

        Require(ttl("missing").result_json == "{\"ttl_seconds\":-2}", "missing ttl not -2");

        auto lock_res = set(R"({"key":"lock:invoice_42","value":"token_abc","if_not_exists":true,"ttl_ms":30000})");
        Require(lock_res.ok && lock_res.result_json == R"({"stored":true})", "lock set failed");
        Require(get("lock:invoice_42").result_json == R"({"value":"token_abc"})", "lock get failed");

        auto lock_ttl = ttl("lock:invoice_42");
        Require(lock_ttl.ok && lock_ttl.result_json.find("{\"ttl_seconds\":") != std::string::npos, "lock ttl failed");

        set(R"({"key":"temp_key","value":"123"})");
        Require(ttl("temp_key").result_json == "{\"ttl_seconds\":-1}", "persistent key ttl not -1");

        auto exp_res = expire(R"({"key":"temp_key","ttl_seconds":60})");
        Require(exp_res.ok && exp_res.result_json == "{\"set\":true}", "expire failed");

        auto pers_res = persist("temp_key");
        Require(pers_res.ok && pers_res.result_json == "{\"persisted\":true}", "persist failed");
        Require(ttl("temp_key").result_json == "{\"ttl_seconds\":-1}", "key not persistent after persist");

        set(R"({"key":"lazy_key","value":"hello","ttl_ms":50})");
        Require(get("lazy_key").ok, "lazy_key should exist immediately");
        std::this_thread::sleep_for(std::chrono::milliseconds(70));
        Require(!get("lazy_key").ok, "lazy_key should expire after sleep");
        Require(ttl("lazy_key").result_json == "{\"ttl_seconds\":-2}", "expired lazy_key ttl not -2");

        set(R"({"key":"active_key_1","value":"v1","ttl_ms":30})");
        set(R"({"key":"active_key_2","value":"v2","ttl_ms":30})");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        const auto purged = store_ptr->PurgeExpired(10);
        Require(purged >= 2, "active purge did not purge expired keys");

        std::cout << "TTL and Expiry engine tests passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
