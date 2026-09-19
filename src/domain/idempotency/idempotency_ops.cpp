#include "gatekeeper/domain/idempotency/idempotency_ops.h"
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

std::string ActionToString(storage::IdempotencyAction action)
{
    switch (action)
    {
    case storage::IdempotencyAction::Execute:
        return "EXECUTE";
    case storage::IdempotencyAction::Park:
        return "PARK";
    case storage::IdempotencyAction::Replay:
        return "REPLAY";
    case storage::IdempotencyAction::Conflict:
        return "CONFLICT";
    }
    return "UNKNOWN";
}

std::string StatusToString(storage::IdempotencyStatus status)
{
    switch (status)
    {
    case storage::IdempotencyStatus::InProgress:
        return "IN_PROGRESS";
    case storage::IdempotencyStatus::Completed:
        return "COMPLETED";
    case storage::IdempotencyStatus::Failed:
        return "FAILED";
    case storage::IdempotencyStatus::None:
        return "NONE";
    }
    return "UNKNOWN";
}

Result IdemBegin(const protocol::Request& request, storage::Store& store)
{
    std::string key;
    std::string request_hash;
    std::uint64_t ttl_ms = 60000;
    std::string owner_token;

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
        else if (field == "request_hash")
        {
            request_hash = reader.String();
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
        else
        {
            throw std::invalid_argument("unsupported GK.IDEM_BEGIN field: " + field);
        }
        if (reader.Take('}'))
        {
            break;
        }
        reader.Expect(',');
    } while (true);
    reader.End();

    if (key.empty())
    {
        throw std::invalid_argument("key must not be empty");
    }
    if (request_hash.empty())
    {
        throw std::invalid_argument("request_hash must not be empty");
    }

    auto result = store.IdemBegin(key, request_hash, ttl_ms, owner_token);
    if (!result.ok)
    {
        return Result{false, {}, {result.error_code, result.error_message}};
    }

    std::string body = "{\"action\":\"" + ActionToString(result.action) + "\"";
    body += ",\"owner_token\":" + protocol::QuoteJson(result.owner_token);
    if (result.action == storage::IdempotencyAction::Replay)
    {
        body += ",\"response_code\":" + std::to_string(result.cached_code);
        body += ",\"response_body\":" + protocol::QuoteJson(result.cached_response);
    }
    body += "}";

    return Result{true, std::move(body), {}};
}

Result IdemComplete(const protocol::Request& request, storage::Store& store)
{
    std::string key;
    std::string owner_token;
    int response_code = 200;
    std::string response_body;

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
        else if (field == "owner_token")
        {
            owner_token = reader.String();
        }
        else if (field == "response_code")
        {
            response_code = static_cast<int>(reader.SignedNumber());
        }
        else if (field == "response_body")
        {
            response_body = reader.String();
        }
        else
        {
            throw std::invalid_argument("unsupported GK.IDEM_COMPLETE field: " + field);
        }
        if (reader.Take('}'))
        {
            break;
        }
        reader.Expect(',');
    } while (true);
    reader.End();

    if (key.empty())
    {
        throw std::invalid_argument("key must not be empty");
    }
    if (owner_token.empty())
    {
        throw std::invalid_argument("owner_token must not be empty");
    }

    auto result = store.IdemComplete(key, owner_token, response_code, response_body);
    if (!result.ok)
    {
        return Result{false, {}, {result.error_code, result.error_message}};
    }

    return Result{true, "{\"completed\":true}", {}};
}

Result IdemFail(const protocol::Request& request, storage::Store& store)
{
    std::string key;
    std::string owner_token;
    std::string error_message;

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
        else if (field == "owner_token")
        {
            owner_token = reader.String();
        }
        else if (field == "error_message")
        {
            error_message = reader.String();
        }
        else
        {
            throw std::invalid_argument("unsupported GK.IDEM_FAIL field: " + field);
        }
        if (reader.Take('}'))
        {
            break;
        }
        reader.Expect(',');
    } while (true);
    reader.End();

    if (key.empty())
    {
        throw std::invalid_argument("key must not be empty");
    }
    if (owner_token.empty())
    {
        throw std::invalid_argument("owner_token must not be empty");
    }

    auto result = store.IdemFail(key, owner_token, error_message);
    if (!result.ok)
    {
        return Result{false, {}, {result.error_code, result.error_message}};
    }

    return Result{true, "{\"failed\":true}", {}};
}

Result IdemGet(const protocol::Request& request, storage::Store& store)
{
    std::string key;

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
        else
        {
            throw std::invalid_argument("unsupported GK.IDEM_GET field: " + field);
        }
        if (reader.Take('}'))
        {
            break;
        }
        reader.Expect(',');
    } while (true);
    reader.End();

    if (key.empty())
    {
        throw std::invalid_argument("key must not be empty");
    }

    auto record = store.IdemGet(key);
    if (!record)
    {
        return Result{false, {}, {"KEY_NOT_FOUND", "idempotency record does not exist or has expired"}};
    }

    std::string body = "{\"key\":" + protocol::QuoteJson(record->key);
    body += ",\"status\":\"" + StatusToString(record->status) + "\"";
    body += ",\"request_hash\":" + protocol::QuoteJson(record->request_hash);
    body += ",\"owner_token\":" + protocol::QuoteJson(record->owner_token);
    body += ",\"response_code\":" + std::to_string(record->response_code);
    body += ",\"response_body\":" + protocol::QuoteJson(record->response_body);
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

void RegisterIdempotencyOps(Dispatcher& dispatcher, storage::Store& store)
{
    dispatcher.Register("GK.IDEM_BEGIN", Validated(IdemBegin, store));
    dispatcher.Register("GK.IDEM_COMPLETE", Validated(IdemComplete, store));
    dispatcher.Register("GK.IDEM_FAIL", Validated(IdemFail, store));
    dispatcher.Register("GK.IDEM_GET", Validated(IdemGet, store));
}

}
