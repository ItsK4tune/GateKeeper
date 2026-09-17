#include "gatekeeper/command/string_ops.h"
#include "gatekeeper/command/string_body.h"
#include "gatekeeper/protocol/response.h"
#include "gatekeeper/storage/store.h"

#include <stdexcept>

namespace gatekeeper::command
{
namespace
{

const std::string& RequiredString(const StringBody& body, const char* field)
{
    const auto it = body.find(field);
    if (it == body.end() || !std::holds_alternative<std::string>(it->second))
        throw std::invalid_argument(std::string(field) + " must be a string");
    return std::get<std::string>(it->second);
}

bool Flag(const StringBody& body, const char* field)
{
    const auto it = body.find(field);
    if (it == body.end()) return false;
    if (!std::holds_alternative<bool>(it->second))
        throw std::invalid_argument(std::string(field) + " must be a boolean");
    return std::get<bool>(it->second);
}

Result Set(const protocol::Request& request, storage::Store& store)
{
    const auto body = ParseStringBody(request.body_json);
    std::uint64_t ttl_ms = 0;
    for (const auto& [name, value] : body)
    {
        if (name == "key" || name == "value" || name == "if_exists" || name == "if_not_exists")
        {
            continue;
        }
        if (name == "ttl_ms")
        {
            if (!std::holds_alternative<std::uint64_t>(value))
            {
                throw std::invalid_argument("ttl_ms must be an unsigned integer");
            }
            ttl_ms = std::get<std::uint64_t>(value);
            if (ttl_ms == 0)
            {
                throw std::invalid_argument("ttl_ms must be greater than zero");
            }
            continue;
        }
        throw std::invalid_argument("unsupported SET field: " + name);
    }
    const auto& key = RequiredString(body, "key");
    const auto& value = RequiredString(body, "value");
    if (key.empty()) throw std::invalid_argument("key must not be empty");
    const bool nx = Flag(body, "if_not_exists"), xx = Flag(body, "if_exists");
    if (nx && xx) throw std::invalid_argument("SET conditions conflict");
    const auto condition = nx ? storage::WriteCondition::IfAbsent
                         : xx ? storage::WriteCondition::IfPresent : storage::WriteCondition::Always;
    return {true, store.Set(key, value, condition, ttl_ms) ? R"({"stored":true})" : R"({"stored":false})", {}};
}

Result Get(const protocol::Request& request, storage::Store& store)
{
    const auto body = ParseStringBody(request.body_json);
    const auto& key = RequiredString(body, "key");
    if (body.size() != 1 || key.empty()) throw std::invalid_argument("GET requires only a non-empty key");
    const auto value = store.Get(key);
    if (!value) return {false, {}, {"KEY_NOT_FOUND", "key does not exist"}};
    return {true, "{\"value\":" + protocol::QuoteJson(*value) + "}", {}};
}

auto Validated(Result (*handler)(const protocol::Request&, storage::Store&),
               storage::Store& store)
{
    return [handler, &store](const protocol::Request& request) {
        try { return handler(request, store); }
        catch (const std::invalid_argument& error) {
            return Result{false, {}, {"INVALID_ARGUMENTS", error.what()}};
        }
    };
}

}

void RegisterStringOps(Dispatcher& dispatcher, storage::Store& store)
{
    dispatcher.Register("SET", Validated(Set, store));
    dispatcher.Register("GET", Validated(Get, store));
}

}
