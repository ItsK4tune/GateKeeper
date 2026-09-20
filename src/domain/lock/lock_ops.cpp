#include "gatekeeper/domain/lock/lock_wait_queue.h"
#include "gatekeeper/domain/lock/lock_ops.h"
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

Result LockAcquire(const protocol::Request& request, storage::Store& store)
{
    std::string resource;
    std::uint64_t ttl_ms = 30000;
    std::string owner_token;
    bool ephemeral = false;
    std::uint64_t session_id = 0;

    JsonReader reader(request.body_json);
    reader.Expect('{');
    do
    {
        auto field = reader.String();
        reader.Expect(':');
        if (field == "resource" || field == "key")
        {
            resource = reader.String();
        }
        else if (field == "ttl_ms")
        {
            ttl_ms = reader.UnsignedNumber();
        }
        else if (field == "ttl_seconds")
        {
            ttl_ms = reader.UnsignedNumber() * 1000;
        }
        else if (field == "owner_token")
        {
            owner_token = reader.String();
        }
        else if (field == "session_id")
        {
            session_id = reader.UnsignedNumber();
        }
        else if (field == "ephemeral")
        {
            ephemeral = reader.Boolean();
        }
        else
        {
            throw std::invalid_argument("unsupported GK.LOCK_ACQUIRE field: " + field);
        }
        if (reader.Take('}'))
        {
            break;
        }
        reader.Expect(',');
    } while (true);
    reader.End();

    if (resource.empty())
    {
        throw std::invalid_argument("resource must not be empty");
    }

    auto result = store.LockAcquire(resource, ttl_ms, owner_token, session_id, ephemeral);
    if (!result.ok)
    {
        return Result{false, {}, {result.error_code, result.error_message}};
    }

    std::string body = std::string("{\"acquired\":") + (result.acquired ? "true" : "false");
    body += ",\"resource\":" + protocol::QuoteJson(result.resource);
    if (result.acquired)
    {
        body += ",\"owner_token\":" + protocol::QuoteJson(result.owner_token);
        body += ",\"fencing_token\":" + std::to_string(result.fencing_token);
        body += ",\"ttl_remaining_ms\":" + std::to_string(result.ttl_remaining_ms);
    }
    else
    {
        body += ",\"error_code\":" + protocol::QuoteJson(result.error_code);
        body += ",\"error_message\":" + protocol::QuoteJson(result.error_message);
        body += ",\"ttl_remaining_ms\":" + std::to_string(result.ttl_remaining_ms);
    }
    body += "}";

    return Result{true, std::move(body), {}};
}

Result LockRelease(const protocol::Request& request, storage::Store& store)
{
    std::string resource;
    std::string owner_token;

    JsonReader reader(request.body_json);
    reader.Expect('{');
    do
    {
        auto field = reader.String();
        reader.Expect(':');
        if (field == "resource" || field == "key")
        {
            resource = reader.String();
        }
        else if (field == "owner_token")
        {
            owner_token = reader.String();
        }
        else
        {
            throw std::invalid_argument("unsupported GK.LOCK_RELEASE field: " + field);
        }
        if (reader.Take('}'))
        {
            break;
        }
        reader.Expect(',');
    } while (true);
    reader.End();

    if (resource.empty())
    {
        throw std::invalid_argument("resource must not be empty");
    }
    if (owner_token.empty())
    {
        throw std::invalid_argument("owner_token must not be empty");
    }

    auto result = store.LockRelease(resource, owner_token);
    if (!result.ok)
    {
        return Result{false, {}, {result.error_code, result.error_message}};
    }

    domain::lock::GetGlobalLockWaitQueue().WakeNext(store, resource);
    return Result{true, "{\"released\":true}", {}};
}

Result LockExtend(const protocol::Request& request, storage::Store& store)
{
    std::string resource;
    std::string owner_token;
    std::uint64_t ttl_ms = 30000;

    JsonReader reader(request.body_json);
    reader.Expect('{');
    do
    {
        auto field = reader.String();
        reader.Expect(':');
        if (field == "resource" || field == "key")
        {
            resource = reader.String();
        }
        else if (field == "owner_token")
        {
            owner_token = reader.String();
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
            throw std::invalid_argument("unsupported GK.LOCK_EXTEND field: " + field);
        }
        if (reader.Take('}'))
        {
            break;
        }
        reader.Expect(',');
    } while (true);
    reader.End();

    if (resource.empty())
    {
        throw std::invalid_argument("resource must not be empty");
    }
    if (owner_token.empty())
    {
        throw std::invalid_argument("owner_token must not be empty");
    }

    auto result = store.LockExtend(resource, owner_token, ttl_ms);
    if (!result.ok)
    {
        return Result{false, {}, {result.error_code, result.error_message}};
    }

    std::string body = "{\"extended\":true,\"fencing_token\":" + std::to_string(result.fencing_token) + ",\"ttl_remaining_ms\":" + std::to_string(result.ttl_remaining_ms) + "}";
    return Result{true, std::move(body), {}};
}

Result LockGet(const protocol::Request& request, storage::Store& store)
{
    std::string resource;

    JsonReader reader(request.body_json);
    reader.Expect('{');
    do
    {
        auto field = reader.String();
        reader.Expect(':');
        if (field == "resource" || field == "key")
        {
            resource = reader.String();
        }
        else
        {
            throw std::invalid_argument("unsupported GK.LOCK_GET field: " + field);
        }
        if (reader.Take('}'))
        {
            break;
        }
        reader.Expect(',');
    } while (true);
    reader.End();

    if (resource.empty())
    {
        throw std::invalid_argument("resource must not be empty");
    }

    auto record = store.LockGet(resource);
    if (!record)
    {
        return Result{false, {}, {"LOCK_NOT_FOUND", "lock does not exist or has expired"}};
    }

    std::string body = "{\"resource\":" + protocol::QuoteJson(record->resource);
    body += ",\"owner_token\":" + protocol::QuoteJson(record->owner_token);
    body += ",\"fencing_token\":" + std::to_string(record->fencing_token);
    body += ",\"is_ephemeral\":" + std::string(record->is_ephemeral ? "true" : "false");
    body += "}";

    return Result{true, std::move(body), {}};
}


Result LockWait(const protocol::Request& request, storage::Store& store)
{
    std::string resource;
    std::uint64_t ttl_ms = 30000;
    std::uint64_t max_wait_ms = 5000;
    std::string owner_token;
    bool ephemeral = false;
    std::uint64_t session_id = 0;

    JsonReader reader(request.body_json);
    reader.Expect('{');
    do
    {
        auto field = reader.String();
        reader.Expect(':');
        if (field == "resource" || field == "key")
        {
            resource = reader.String();
        }
        else if (field == "ttl_ms")
        {
            ttl_ms = reader.UnsignedNumber();
        }
        else if (field == "ttl_seconds")
        {
            ttl_ms = reader.UnsignedNumber() * 1000;
        }
        else if (field == "max_wait_ms")
        {
            max_wait_ms = reader.UnsignedNumber();
        }
        else if (field == "max_wait_seconds")
        {
            max_wait_ms = reader.UnsignedNumber() * 1000;
        }
        else if (field == "owner_token")
        {
            owner_token = reader.String();
        }
        else if (field == "session_id")
        {
            session_id = reader.UnsignedNumber();
        }
        else if (field == "ephemeral")
        {
            ephemeral = reader.Boolean();
        }
        else
        {
            throw std::invalid_argument("unsupported GK.LOCK_WAIT field: " + field);
        }
        if (reader.Take('}'))
        {
            break;
        }
        reader.Expect(',');
    } while (true);
    reader.End();

    if (resource.empty())
    {
        throw std::invalid_argument("resource must not be empty");
    }

    auto result = domain::lock::GetGlobalLockWaitQueue().WaitOrAcquire(
        store, resource, ttl_ms, max_wait_ms, owner_token, session_id, ephemeral);

    if (!result.ok && result.error_code == "ERR_LOCK_WAIT_TIMEOUT")
    {
        return Result{false, {}, {result.error_code, result.error_message}};
    }
    if (!result.ok)
    {
        return Result{false, {}, {result.error_code, result.error_message}};
    }

    std::string body = std::string("{\"acquired\":") + (result.acquired ? "true" : "false");
    body += ",\"resource\":" + protocol::QuoteJson(result.resource);
    if (result.acquired)
    {
        body += ",\"owner_token\":" + protocol::QuoteJson(result.owner_token);
        body += ",\"fencing_token\":" + std::to_string(result.fencing_token);
        body += ",\"ttl_remaining_ms\":" + std::to_string(result.ttl_remaining_ms);
    }
    else
    {
        body += ",\"error_code\":" + protocol::QuoteJson(result.error_code);
        body += ",\"error_message\":" + protocol::QuoteJson(result.error_message);
        body += ",\"ttl_remaining_ms\":" + std::to_string(result.ttl_remaining_ms);
    }
    body += "}";

    return Result{true, std::move(body), {}};
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

void RegisterLockOps(Dispatcher& dispatcher, storage::Store& store)
{
    dispatcher.Register("GK.LOCK_ACQUIRE", Validated(LockAcquire, store));
    dispatcher.Register("GK.LOCK_RELEASE", Validated(LockRelease, store));
    dispatcher.Register("GK.LOCK_EXTEND", Validated(LockExtend, store));
    dispatcher.Register("GK.LOCK_GET", Validated(LockGet, store));
    dispatcher.Register("GK.LOCK_WAIT", Validated(LockWait, store));
}

}
