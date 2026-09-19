#include "gatekeeper/domain/quota/reservation_ops.h"
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

Result Reserve(const protocol::Request& request, storage::Store& store)
{
    std::string key;
    std::uint64_t amount = 0;
    bool has_amount = false;
    std::uint64_t ttl_ms = 0;
    bool has_ttl = false;

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
        else if (field == "amount")
        {
            amount = reader.UnsignedNumber();
            has_amount = true;
        }
        else if (field == "ttl_ms")
        {
            ttl_ms = reader.UnsignedNumber();
            has_ttl = true;
        }
        else if (field == "ttl_seconds")
        {
            ttl_ms = reader.UnsignedNumber() * 1000;
            has_ttl = true;
        }
        else
        {
            throw std::invalid_argument("unsupported GK.RESERVE field: " + field);
        }
        if (reader.Take('}'))
        {
            break;
        }
        reader.Expect(',');
    } while (true);
    reader.End();

    if (key.empty() || !has_amount || !has_ttl)
    {
        throw std::invalid_argument("GK.RESERVE requires key, amount and ttl_ms");
    }

    const auto res = store.ReserveQuota(key, amount, ttl_ms);
    if (!res.ok || !res.reserved)
    {
        return Result{false, {}, {res.error_code, res.error_message}};
    }

    std::string result_json = "{\"reserved\":true,\"reservation_id\":" + protocol::QuoteJson(res.reservation_id) +
                              ",\"remaining\":" + std::to_string(res.remaining) + "}";
    return Result{true, std::move(result_json), {}};
}

Result Commit(const protocol::Request& request, storage::Store& store)
{
    std::string key;
    std::string reservation_id;
    std::uint64_t actual_amount = 0;
    bool has_actual = false;

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
        else if (field == "reservation_id")
        {
            reservation_id = reader.String();
        }
        else if (field == "actual_amount" || field == "amount")
        {
            actual_amount = reader.UnsignedNumber();
            has_actual = true;
        }
        else
        {
            throw std::invalid_argument("unsupported GK.COMMIT field: " + field);
        }
        if (reader.Take('}'))
        {
            break;
        }
        reader.Expect(',');
    } while (true);
    reader.End();

    if (key.empty() || reservation_id.empty() || !has_actual)
    {
        throw std::invalid_argument("GK.COMMIT requires key, reservation_id and actual_amount");
    }

    const auto res = store.CommitQuota(key, reservation_id, actual_amount);
    if (!res.ok || !res.committed)
    {
        return Result{false, {}, {res.error_code, res.error_message}};
    }

    std::string result_json = "{\"committed\":true,\"actual_amount\":" + std::to_string(res.actual_amount) +
                              ",\"refunded\":" + std::to_string(res.refunded) +
                              ",\"remaining\":" + std::to_string(res.remaining) + "}";
    return Result{true, std::move(result_json), {}};
}

Result Rollback(const protocol::Request& request, storage::Store& store)
{
    std::string key;
    std::string reservation_id;

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
        else if (field == "reservation_id")
        {
            reservation_id = reader.String();
        }
        else
        {
            throw std::invalid_argument("unsupported GK.ROLLBACK field: " + field);
        }
        if (reader.Take('}'))
        {
            break;
        }
        reader.Expect(',');
    } while (true);
    reader.End();

    if (key.empty() || reservation_id.empty())
    {
        throw std::invalid_argument("GK.ROLLBACK requires key and reservation_id");
    }

    const auto res = store.RollbackQuota(key, reservation_id);
    if (!res.ok || !res.rolled_back)
    {
        return Result{false, {}, {res.error_code, res.error_message}};
    }

    std::string result_json = "{\"rolled_back\":true,\"refunded\":" + std::to_string(res.refunded) +
                              ",\"remaining\":" + std::to_string(res.remaining) + "}";
    return Result{true, std::move(result_json), {}};
}

Result QuotaInit(const protocol::Request& request, storage::Store& store)
{
    std::string key;
    std::uint64_t amount = 0;
    bool has_amount = false;

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
        else if (field == "amount" || field == "balance")
        {
            amount = reader.UnsignedNumber();
            has_amount = true;
        }
        else
        {
            throw std::invalid_argument("unsupported GK.QUOTA_INIT field: " + field);
        }
        if (reader.Take('}'))
        {
            break;
        }
        reader.Expect(',');
    } while (true);
    reader.End();

    if (key.empty() || !has_amount)
    {
        throw std::invalid_argument("GK.QUOTA_INIT requires key and amount");
    }

    store.Set(key, std::to_string(amount), storage::WriteCondition::Always);
    return Result{true, "{\"initialized\":true,\"key\":" + protocol::QuoteJson(key) + ",\"balance\":" + std::to_string(amount) + "}", {}};
}

Result QuotaGet(const protocol::Request& request, storage::Store& store)
{
    JsonReader reader(request.body_json);
    reader.Expect('{');
    auto field = reader.String();
    reader.Expect(':');
    if (field != "key")
    {
        throw std::invalid_argument("GK.QUOTA_GET requires key field");
    }
    const auto key = reader.String();
    reader.Expect('}');
    reader.End();

    if (key.empty())
    {
        throw std::invalid_argument("key must not be empty");
    }

    const auto val = store.Get(key);
    if (!val)
    {
        return Result{false, {}, {"KEY_NOT_FOUND", "quota key does not exist"}};
    }

    std::uint64_t balance = 0;
    try
    {
        balance = std::stoull(*val);
    }
    catch (...)
    {
        return Result{false, {}, {"ERR_NOT_AN_INTEGER", "quota balance is not an integer"}};
    }

    return Result{true, "{\"key\":" + protocol::QuoteJson(key) + ",\"balance\":" + std::to_string(balance) + "}", {}};
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

void RegisterReservationOps(Dispatcher& dispatcher, storage::Store& store)
{
    dispatcher.Register("GK.RESERVE", Validated(Reserve, store));
    dispatcher.Register("GK.COMMIT", Validated(Commit, store));
    dispatcher.Register("GK.ROLLBACK", Validated(Rollback, store));
    dispatcher.Register("GK.QUOTA_INIT", Validated(QuotaInit, store));
    dispatcher.Register("GK.QUOTA_GET", Validated(QuotaGet, store));
}

}
