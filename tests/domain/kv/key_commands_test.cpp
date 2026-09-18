#include "gatekeeper/command/dispatcher.h"

#include <iostream>
#include <stdexcept>
#include <string>

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
        Dispatcher d;
        auto set = [&](const std::string& key, const std::string& val) {
            return d.Dispatch({"1", "SET", "{\"key\":\"" + key + "\",\"value\":\"" + val + "\"}"});
        };
        auto get = [&](const std::string& key) {
            return d.Dispatch({"2", "GET", "{\"key\":\"" + key + "\"}"});
        };
        auto del = [&](const std::string& body) {
            return d.Dispatch({"3", "DEL", body});
        };
        auto exists = [&](const std::string& body) {
            return d.Dispatch({"4", "EXISTS", body});
        };
        auto type = [&](const std::string& key) {
            return d.Dispatch({"5", "TYPE", "{\"key\":\"" + key + "\"}"});
        };
        auto dbsize = [&]() {
            return d.Dispatch({"6", "DBSIZE", "{}"});
        };
        auto keys = [&](const std::string& pattern) {
            return d.Dispatch({"7", "KEYS", "{\"pattern\":\"" + pattern + "\"}"});
        };
        auto scan = [&](const std::string& body) {
            return d.Dispatch({"8", "SCAN", body});
        };

        Require(dbsize().result_json == "{\"size\":0}", "initial dbsize not 0");

        set("user:1", "alice");
        set("user:2", "bob");
        set("order:1", "apple");

        Require(dbsize().result_json == "{\"size\":3}", "dbsize not 3");
        Require(type("user:1").result_json == "{\"type\":\"string\"}", "type user:1 not string");
        Require(type("unknown").result_json == "{\"type\":\"none\"}", "type unknown not none");

        Require(exists("{\"key\":\"user:1\"}").result_json == "{\"count\":1}", "exists user:1 wrong");
        Require(exists("{\"keys\":[\"user:1\",\"user:2\",\"missing\"]}").result_json == "{\"count\":2}", "exists multiple wrong");

        Require(del("{\"key\":\"user:1\"}").result_json == "{\"deleted\":1}", "del single wrong");
        Require(!get("user:1").ok, "user:1 still exists after del");
        Require(dbsize().result_json == "{\"size\":2}", "dbsize after del wrong");

        Require(del("{\"keys\":[\"user:2\",\"order:1\",\"user:2\"]}").result_json == "{\"deleted\":2}", "del multiple wrong");
        Require(dbsize().result_json == "{\"size\":0}", "dbsize after multiple del wrong");

        set("prefix:abc", "1");
        set("prefix:xyz", "2");
        set("suffix:test", "3");

        auto k_res = keys("prefix:*");
        Require(k_res.ok, "keys pattern failed");
        Require(k_res.result_json.find("prefix:abc") != std::string::npos, "missing prefix:abc");
        Require(k_res.result_json.find("prefix:xyz") != std::string::npos, "missing prefix:xyz");
        Require(k_res.result_json.find("suffix:test") == std::string::npos, "unmatched key in pattern");

        auto scan_res = scan("{\"cursor\":0,\"count\":10}");
        Require(scan_res.ok, "scan failed");
        Require(scan_res.result_json.find("\"keys\":[") != std::string::npos, "scan missing keys");

        std::cout << "Key commands tests passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
