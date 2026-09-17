#include "gatekeeper/command/dispatcher.h"
#include <stdexcept>
#include <iostream>
using gatekeeper::command::Dispatcher;
void Require(bool ok) { if (!ok) throw std::runtime_error("string command contract failed"); }
int main() {
    Dispatcher d;
    auto set = [&](std::string body) { return d.Dispatch({"1", "sEt", body}); };
    auto get = [&](std::string key) { return d.Dispatch({"2", "gEt", "{\"key\":\"" + key + "\"}"}); };
    Require(set(R"({"key":"k","value":"first","if_not_exists":true})").result_json == R"({"stored":true})");
    Require(set(R"({"key":"k","value":"second","if_not_exists":true})").result_json == R"({"stored":false})");
    Require(get("k").result_json == R"({"value":"first"})");
    Require(set(R"({"key":"missing","value":"x","if_exists":true})").result_json == R"({"stored":false})");
    Require(!get("missing").ok);
    Require(set(R"({"key":"k","value":"","if_exists":true})").result_json == R"({"stored":true})");
    Require(get("k").result_json == R"({"value":""})");
    for (const auto* body : {
        R"({"key":"k","value":"bad","if_exists":"true"})",
        R"({"key":"k","value":"bad","if_exists":true,"if_not_exists":true})",
        R"({"key":"k","value":"bad","key":"other"})",
        R"({"key":"k","value":"bad",})",
        R"({"nested":{"key":"k","value":"bad"}})",
        R"({"key":"k","value":12})",
        R"({"key":"k","value":"bad","ttl_ms":1})"
    }) {
        const auto result = set(body);
        Require(!result.ok && result.error.code == "INVALID_ARGUMENTS");
        Require(get("k").result_json == R"({"value":""})");
    }
    Require(set(R"({"key":"k","value":"quote:\" slash:\\ newline:\n"})").ok);
    Require(get("k").result_json == R"({"value":"quote:\" slash:\\ newline:\n"})");
    Require(set(R"({"key":"k","value":"\u0061\uD83D\uDE00"})").ok);
    Require(get("k").result_json == "{\"value\":\"a😀\"}");
    std::cout << "String commands passed\n";
}
