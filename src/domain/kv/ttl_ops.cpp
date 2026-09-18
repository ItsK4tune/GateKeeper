#include "gatekeeper/domain/kv/ttl_ops.h"
#include "gatekeeper/protocol/gkwp/json_reader.h"
#include "gatekeeper/protocol/gkwp/response.h"

#include <cstdint>
#include <stdexcept>
#include <string>

namespace gatekeeper::command
{
namespace
{

using protocol::JsonReader;

Result Expire(const protocol::Request& request, storage::Store& store)
{
    std::string key;
    std::uint64_t seconds = 0;
    bool has_seconds = false;

    JsonReader reader(request.body_json);
    reader.Expect('{');
    do
    {
        auto field = reader.String();
        reader.Expect(':');
        if (field == "key")
        {
            key = reader.String();
        }
        else if (field == "ttl_seconds" || field == "seconds")
        {
            seconds = reader.UnsignedNumber();
            has_seconds = true;
        }
        else
        {
            throw std::invalid_argument("unsupported EXPIRE field: " + field);
        }
        if (reader.Take('}'))
        {
            break;
        }
        reader.Expect(',');
    } while (true);
    reader.End();

    if (key.empty() || !has_seconds)
    {
        throw std::invalid_argument("EXPIRE requires key and ttl_seconds");
    }

    const bool set = store.Expire(key, seconds * 1000);
    return {true, std::string("{\"set\":") + (set ? "true}" : "false}"), {}};
}

Result PExpire(const protocol::Request& request, storage::Store& store)
{
    std::string key;
    std::uint64_t ms = 0;
    bool has_ms = false;

    JsonReader reader(request.body_json);
    reader.Expect('{');
    do
    {
        auto field = reader.String();
        reader.Expect(':');
        if (field == "key")
        {
            key = reader.String();
        }
        else if (field == "ttl_ms" || field == "milliseconds")
        {
            ms = reader.UnsignedNumber();
            has_ms = true;
        }
        else
        {
            throw std::invalid_argument("unsupported PEXPIRE field: " + field);
        }
        if (reader.Take('}'))
        {
            break;
        }
        reader.Expect(',');
    } while (true);
    reader.End();

    if (key.empty() || !has_ms)
    {
        throw std::invalid_argument("PEXPIRE requires key and ttl_ms");
    }

    const bool set = store.Expire(key, ms);
    return {true, std::string("{\"set\":") + (set ? "true}" : "false}"), {}};
}

Result Ttl(const protocol::Request& request, storage::Store& store)
{
    JsonReader reader(request.body_json);
    reader.Expect('{');
    auto field = reader.String();
    reader.Expect(':');
    if (field != "key")
    {
        throw std::invalid_argument("TTL requires key field");
    }
    const auto key = reader.String();
    reader.Expect('}');
    reader.End();

    if (key.empty())
    {
        throw std::invalid_argument("key must not be empty");
    }

    const auto ttl = store.Ttl(key);
    return {true, "{\"ttl_seconds\":" + std::to_string(ttl) + "}", {}};
}

Result Pttl(const protocol::Request& request, storage::Store& store)
{
    JsonReader reader(request.body_json);
    reader.Expect('{');
    auto field = reader.String();
    reader.Expect(':');
    if (field != "key")
    {
        throw std::invalid_argument("PTTL requires key field");
    }
    const auto key = reader.String();
    reader.Expect('}');
    reader.End();

    if (key.empty())
    {
        throw std::invalid_argument("key must not be empty");
    }

    const auto pttl = store.Pttl(key);
    return {true, "{\"ttl_ms\":" + std::to_string(pttl) + "}", {}};
}

Result Persist(const protocol::Request& request, storage::Store& store)
{
    JsonReader reader(request.body_json);
    reader.Expect('{');
    auto field = reader.String();
    reader.Expect(':');
    if (field != "key")
    {
        throw std::invalid_argument("PERSIST requires key field");
    }
    const auto key = reader.String();
    reader.Expect('}');
    reader.End();

    if (key.empty())
    {
        throw std::invalid_argument("key must not be empty");
    }

    const bool persisted = store.Persist(key);
    return {true, std::string("{\"persisted\":") + (persisted ? "true}" : "false}"), {}};
}

auto Validated(Result (*handler)(const protocol::Request&, storage::Store&),
               storage::Store& store)
{
    return [handler, &store](const protocol::Request& request) {
        try
        {
            return handler(request, store);
        }
        catch (const std::exception& error)
        {
            return Result{false, {}, {"INVALID_ARGUMENTS", error.what()}};
        }
    };
}

}

void RegisterTtlOps(Dispatcher& dispatcher, storage::Store& store)
{
    dispatcher.Register("EXPIRE", Validated(Expire, store));
    dispatcher.Register("PEXPIRE", Validated(PExpire, store));
    dispatcher.Register("TTL", Validated(Ttl, store));
    dispatcher.Register("PTTL", Validated(Pttl, store));
    dispatcher.Register("PERSIST", Validated(Persist, store));
}

}
