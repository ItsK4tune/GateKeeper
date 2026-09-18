#include "gatekeeper/command/counter_ops.h"
#include "gatekeeper/protocol/json_reader.h"
#include "gatekeeper/protocol/response.h"

#include <cstdint>
#include <stdexcept>
#include <string>

namespace gatekeeper::command
{
namespace
{

using protocol::JsonReader;

Result Incr(const protocol::Request& request, storage::Store& store)
{
    JsonReader reader(request.body_json);
    reader.Expect('{');
    auto field = reader.String();
    reader.Expect(':');
    if (field != "key")
    {
        throw std::invalid_argument("INCR requires key field");
    }
    const auto key = reader.String();
    reader.Expect('}');
    reader.End();

    if (key.empty())
    {
        throw std::invalid_argument("key must not be empty");
    }

    const auto res = store.IncrBy(key, 1);
    if (!res.ok)
    {
        return Result{false, {}, {res.error_code, res.error_message}};
    }
    return Result{true, "{\"value\":" + std::to_string(res.value) + "}", {}};
}

Result Decr(const protocol::Request& request, storage::Store& store)
{
    JsonReader reader(request.body_json);
    reader.Expect('{');
    auto field = reader.String();
    reader.Expect(':');
    if (field != "key")
    {
        throw std::invalid_argument("DECR requires key field");
    }
    const auto key = reader.String();
    reader.Expect('}');
    reader.End();

    if (key.empty())
    {
        throw std::invalid_argument("key must not be empty");
    }

    const auto res = store.IncrBy(key, -1);
    if (!res.ok)
    {
        return Result{false, {}, {res.error_code, res.error_message}};
    }
    return Result{true, "{\"value\":" + std::to_string(res.value) + "}", {}};
}

Result IncrBy(const protocol::Request& request, storage::Store& store)
{
    std::string key;
    std::int64_t delta = 0;
    bool has_delta = false;
    std::uint64_t ttl_ms = 0;

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
        else if (field == "delta" || field == "by")
        {
            delta = reader.SignedNumber();
            has_delta = true;
        }
        else if (field == "ttl_ms")
        {
            ttl_ms = reader.UnsignedNumber();
        }
        else if (field == "ttl_seconds")
        {
            ttl_ms = reader.UnsignedNumber() * 1000;
        }
        else
        {
            throw std::invalid_argument("unsupported INCRBY field: " + field);
        }
        if (reader.Take('}'))
        {
            break;
        }
        reader.Expect(',');
    } while (true);
    reader.End();

    if (key.empty() || !has_delta)
    {
        throw std::invalid_argument("INCRBY requires key and delta");
    }

    const auto res = store.IncrBy(key, delta, ttl_ms);
    if (!res.ok)
    {
        return Result{false, {}, {res.error_code, res.error_message}};
    }
    return Result{true, "{\"value\":" + std::to_string(res.value) + "}", {}};
}

Result RateLimit(const protocol::Request& request, storage::Store& store)
{
    std::string key;
    std::uint64_t limit = 0;
    bool has_limit = false;
    std::uint64_t window_ms = 0;
    bool has_window = false;
    std::uint64_t cost = 1;

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
        else if (field == "limit")
        {
            limit = reader.UnsignedNumber();
            has_limit = true;
        }
        else if (field == "window_ms")
        {
            window_ms = reader.UnsignedNumber();
            has_window = true;
        }
        else if (field == "window_seconds")
        {
            window_ms = reader.UnsignedNumber() * 1000;
            has_window = true;
        }
        else if (field == "cost")
        {
            cost = reader.UnsignedNumber();
        }
        else
        {
            throw std::invalid_argument("unsupported GK.RATE_LIMIT field: " + field);
        }
        if (reader.Take('}'))
        {
            break;
        }
        reader.Expect(',');
    } while (true);
    reader.End();

    if (key.empty() || !has_limit || !has_window)
    {
        throw std::invalid_argument("GK.RATE_LIMIT requires key, limit and window_ms");
    }

    const auto res = store.RateLimit(key, limit, window_ms, cost);
    if (!res.ok)
    {
        return Result{false, {}, {res.error_code, res.error_message}};
    }

    std::string result_json = "{\"allowed\":" + std::string(res.allowed ? "true" : "false") +
                              ",\"remaining\":" + std::to_string(res.remaining) +
                              ",\"retry_after_ms\":" + std::to_string(res.retry_after_ms) + "}";
    return Result{true, std::move(result_json), {}};
}

auto Validated(Result (*handler)(const protocol::Request&, storage::Store&), storage::Store& store)
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

void RegisterCounterOps(Dispatcher& dispatcher, storage::Store& store)
{
    dispatcher.Register("INCR", Validated(Incr, store));
    dispatcher.Register("DECR", Validated(Decr, store));
    dispatcher.Register("INCRBY", Validated(IncrBy, store));
    dispatcher.Register("GK.RATE_LIMIT", Validated(RateLimit, store));
}

}
