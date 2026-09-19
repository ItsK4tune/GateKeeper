#include "gatekeeper/storage/aof/aof_writer.h"
#include "gatekeeper/service/http_service.h"
#include "gatekeeper/protocol/json_reader.h"
#include "gatekeeper/protocol/response.h"

#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace gatekeeper::service
{

namespace
{

std::string UrlDecode(std::string_view in)
{
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i)
    {
        if (in[i] == '%' && i + 2 < in.size())
        {
            auto hex_val = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int h1 = hex_val(in[i + 1]);
            int h2 = hex_val(in[i + 2]);
            if (h1 != -1 && h2 != -1)
            {
                out += static_cast<char>((h1 << 4) | h2);
                i += 2;
                continue;
            }
        }
        else if (in[i] == '+')
        {
            out += ' ';
            continue;
        }
        out += in[i];
    }
    return out;
}

std::string ExtractKeyFromQuery(std::string_view query)
{
    constexpr std::string_view prefix = "key=";
    auto pos = query.find(prefix);
    while (pos != std::string_view::npos)
    {
        if (pos == 0 || query[pos - 1] == '&')
        {
            auto val_start = pos + prefix.size();
            auto val_end = query.find('&', val_start);
            if (val_end == std::string_view::npos)
            {
                return UrlDecode(query.substr(val_start));
            }
            return UrlDecode(query.substr(val_start, val_end - val_start));
        }
        pos = query.find(prefix, pos + 1);
    }
    return {};
}

}

HttpService::HttpService(
    storage::Store& store,
    std::shared_ptr<log::Logger> logger,
    std::shared_ptr<storage::aof::AofWriter> aof_writer)
    : store_(store), logger_(std::move(logger)), aof_writer_(std::move(aof_writer))
{
    if (!logger_)
    {
        logger_ = log::Logger::Null();
    }
}

net::HttpResponse HttpService::Handle(const net::HttpRequest& req)
{
    if (req.method == "GET" && req.path == "/healthz")
    {
        return HandleHealthz(req);
    }
    if (req.method == "POST" && req.path == "/v1/rate-limit/check")
    {
        return HandleRateLimitCheck(req);
    }
    if (req.method == "POST" && req.path == "/v1/quota/reserve")
    {
        return HandleQuotaReserve(req);
    }
    if (req.method == "POST" && req.path == "/v1/quota/commit")
    {
        return HandleQuotaCommit(req);
    }
    if (req.method == "POST" && req.path == "/v1/quota/rollback")
    {
        return HandleQuotaRollback(req);
    }
    if (req.method == "POST" && req.path == "/v1/quota/init")
    {
        return HandleQuotaInit(req);
    }
    if (req.method == "POST" && req.path == "/v1/idempotency/begin")
    {
        return HandleIdempotencyBegin(req);
    }
    if (req.method == "POST" && req.path == "/v1/idempotency/complete")
    {
        return HandleIdempotencyComplete(req);
    }
    if (req.method == "POST" && req.path == "/v1/idempotency/fail")
    {
        return HandleIdempotencyFail(req);
    }
    if (req.method == "POST" && req.path == "/v1/kv/set")
    {
        return HandleKvSet(req);
    }
    if (req.method == "GET" && req.path == "/v1/kv/get")
    {
        return HandleKvGet(req);
    }
    if (req.method == "POST" && req.path == "/v1/kv/del")
    {
        return HandleKvDel(req);
    }
    if (req.method == "POST" && req.path == "/v1/kv/exists")
    {
        return HandleKvExists(req);
    }
    if (req.method == "GET" && req.path == "/v1/kv/type")
    {
        return HandleKvType(req);
    }
    if (req.method == "POST" && req.path == "/v1/kv/expire")
    {
        return HandleKvExpire(req);
    }
    if (req.method == "GET" && req.path == "/v1/kv/ttl")
    {
        return HandleKvTtl(req);
    }

    if (req.path == "/healthz" || req.path == "/v1/rate-limit/check" ||
        req.path == "/v1/quota/reserve" || req.path == "/v1/quota/commit" ||
        req.path == "/v1/quota/rollback" || req.path == "/v1/quota/init" ||
        req.path == "/v1/idempotency/begin" || req.path == "/v1/idempotency/complete" ||
        req.path == "/v1/idempotency/fail" ||
        req.path == "/v1/kv/set" || req.path == "/v1/kv/get" ||
        req.path == "/v1/kv/del" || req.path == "/v1/kv/exists" ||
        req.path == "/v1/kv/type" || req.path == "/v1/kv/expire" ||
        req.path == "/v1/kv/ttl")
    {
        net::HttpResponse res;
        res.status_code = 405;
        res.status_text = "Method Not Allowed";
        res.body = "{\"error\":\"METHOD_NOT_ALLOWED\"}";
        return res;
    }

    net::HttpResponse res;
    res.status_code = 404;
    res.status_text = "Not Found";
    res.body = "{\"error\":\"NOT_FOUND\"}";
    return res;
}

net::HttpResponse HttpService::HandleHealthz(const net::HttpRequest& /*req*/)
{
    net::HttpResponse resp;
    resp.status_code = 200;
    resp.status_text = "OK";
    resp.body = "{\"status\":\"ok\"}";
    return resp;
}

net::HttpResponse HttpService::HandleRateLimitCheck(const net::HttpRequest& req)
{
    if (req.body.empty())
    {
        return BadRequest("missing JSON body");
    }

    std::string key;
    std::string tenant = "default";
    std::string subject = "anonymous";
    std::string resource = "default";
    std::uint64_t limit = 0;
    bool has_limit = false;
    std::uint64_t window_ms = 0;
    bool has_window = false;
    std::uint64_t cost = 1;

    try
    {
        protocol::JsonReader reader(req.body);
        reader.Expect('{');
        do
        {
            auto field = reader.String();
            reader.Expect(':');
            if (field == "key")
            {
                key = reader.String();
            }
            else if (field == "tenant")
            {
                tenant = reader.String();
            }
            else if (field == "subject")
            {
                subject = reader.String();
            }
            else if (field == "resource")
            {
                resource = reader.String();
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
                throw std::invalid_argument("unsupported field: " + field);
            }

            if (reader.Take('}'))
            {
                break;
            }
            reader.Expect(',');
        } while (true);
        reader.End();
    }
    catch (const std::exception& ex)
    {
        return BadRequest(std::string("invalid JSON: ") + ex.what());
    }

    if (!has_limit || limit == 0)
    {
        return BadRequest("limit is required and must be greater than 0");
    }
    if (!has_window || window_ms == 0)
    {
        return BadRequest("window_ms is required and must be greater than 0");
    }

    if (key.empty())
    {
        key = "gk:rate:" + tenant + ":" + subject + ":" + resource;
    }

    const auto res = store_.RateLimit(key, limit, window_ms, cost);
    const auto now_sec = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());

    net::HttpResponse resp;
    resp.SetHeader("X-RateLimit-Limit", std::to_string(limit));
    resp.SetHeader("X-RateLimit-Remaining", std::to_string(res.remaining));

    if (res.allowed)
    {
        const auto reset_sec = now_sec + (window_ms / 1000) + 1;
        resp.SetHeader("X-RateLimit-Reset", std::to_string(reset_sec));
        resp.status_code = 200;
        resp.status_text = "OK";
        resp.body = "{\"allowed\":true,\"remaining\":" + std::to_string(res.remaining) +
                    ",\"retry_after_ms\":0}";
    }
    else
    {
        const auto retry_after_sec = (res.retry_after_ms + 999) / 1000;
        const auto reset_sec = now_sec + retry_after_sec;
        resp.SetHeader("X-RateLimit-Reset", std::to_string(reset_sec));
        resp.SetHeader("Retry-After", std::to_string(retry_after_sec));
        resp.status_code = 429;
        resp.status_text = "Too Many Requests";
        resp.body = "{\"allowed\":false,\"remaining\":0,\"retry_after_ms\":" +
                    std::to_string(res.retry_after_ms) + "}";
    }
    return resp;
}

net::HttpResponse HttpService::HandleQuotaReserve(const net::HttpRequest& req)
{
    if (req.body.empty())
    {
        return BadRequest("missing JSON body");
    }

    std::string key;
    std::uint64_t amount = 0;
    bool has_amount = false;
    std::uint64_t ttl_ms = 0;
    bool has_ttl = false;

    try
    {
        protocol::JsonReader reader(req.body);
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
                throw std::invalid_argument("unsupported field: " + field);
            }

            if (reader.Take('}'))
            {
                break;
            }
            reader.Expect(',');
        } while (true);
        reader.End();
    }
    catch (const std::exception& ex)
    {
        return BadRequest(std::string("invalid JSON: ") + ex.what());
    }

    if (key.empty())
    {
        return BadRequest("key is required");
    }
    if (!has_amount || amount == 0)
    {
        return BadRequest("amount is required and must be greater than 0");
    }
    if (!has_ttl || ttl_ms == 0)
    {
        return BadRequest("ttl_ms is required and must be greater than 0");
    }

    const auto res = store_.ReserveQuota(key, amount, ttl_ms);
    if (!res.ok)
    {
        net::HttpResponse resp;
        resp.status_code = 400;
        resp.status_text = "Bad Request";
        resp.body = "{\"error\":\"" + res.error_code + "\",\"message\":\"" + res.error_message + "\"}";
        return resp;
    }

    net::HttpResponse resp;
    if (res.reserved)
    {
        if (aof_writer_)
        {
            std::string aof_body = "{\"key\":" + protocol::QuoteJson(key) +
                                   ",\"amount\":" + std::to_string(amount) +
                                   ",\"ttl_ms\":" + std::to_string(ttl_ms) + "}";
            aof_writer_->Append("GK.RESERVE", aof_body);
        }
        resp.status_code = 200;
        resp.status_text = "OK";
        resp.body = "{\"reserved\":true,\"remaining\":" + std::to_string(res.remaining) +
                    ",\"reservation_id\":\"" + res.reservation_id + "\"}";
    }
    else
    {
        resp.status_code = 429;
        resp.status_text = "Too Many Requests";
        resp.body = "{\"reserved\":false,\"remaining\":" + std::to_string(res.remaining) +
                    ",\"reservation_id\":\"\"}";
    }
    return resp;
}

net::HttpResponse HttpService::HandleQuotaCommit(const net::HttpRequest& req)
{
    if (req.body.empty())
    {
        return BadRequest("missing JSON body");
    }

    std::string key;
    std::string res_id;
    std::uint64_t actual_amount = 0;
    bool has_actual = false;

    try
    {
        protocol::JsonReader reader(req.body);
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
                res_id = reader.String();
            }
            else if (field == "actual_amount")
            {
                actual_amount = reader.UnsignedNumber();
                has_actual = true;
            }
            else
            {
                throw std::invalid_argument("unsupported field: " + field);
            }

            if (reader.Take('}'))
            {
                break;
            }
            reader.Expect(',');
        } while (true);
        reader.End();
    }
    catch (const std::exception& ex)
    {
        return BadRequest(std::string("invalid JSON: ") + ex.what());
    }

    if (key.empty() || res_id.empty() || !has_actual)
    {
        return BadRequest("key, reservation_id, and actual_amount are required");
    }

    const auto res = store_.CommitQuota(key, res_id, actual_amount);
    if (!res.ok)
    {
        net::HttpResponse resp;
        resp.status_code = 400;
        resp.status_text = "Bad Request";
        resp.body = "{\"error\":\"" + res.error_code + "\",\"message\":\"" + res.error_message + "\"}";
        return resp;
    }

    net::HttpResponse resp;
    resp.status_code = 200;
    resp.status_text = "OK";
    resp.body = "{\"committed\":true,\"actual_amount\":" + std::to_string(res.actual_amount) +
                ",\"refunded\":" + std::to_string(res.refunded) +
                ",\"remaining\":" + std::to_string(res.remaining) + "}";
    return resp;
}

net::HttpResponse HttpService::HandleQuotaRollback(const net::HttpRequest& req)
{
    if (req.body.empty())
    {
        return BadRequest("missing JSON body");
    }

    std::string key;
    std::string res_id;

    try
    {
        protocol::JsonReader reader(req.body);
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
                res_id = reader.String();
            }
            else
            {
                throw std::invalid_argument("unsupported field: " + field);
            }

            if (reader.Take('}'))
            {
                break;
            }
            reader.Expect(',');
        } while (true);
        reader.End();
    }
    catch (const std::exception& ex)
    {
        return BadRequest(std::string("invalid JSON: ") + ex.what());
    }

    if (key.empty() || res_id.empty())
    {
        return BadRequest("key and reservation_id are required");
    }

    const auto res = store_.RollbackQuota(key, res_id);
    if (!res.ok)
    {
        net::HttpResponse resp;
        resp.status_code = 400;
        resp.status_text = "Bad Request";
        resp.body = "{\"error\":\"" + res.error_code + "\",\"message\":\"" + res.error_message + "\"}";
        return resp;
    }

    net::HttpResponse resp;
    resp.status_code = 200;
    resp.status_text = "OK";
    resp.body = "{\"rolled_back\":true,\"refunded\":" + std::to_string(res.refunded) +
                ",\"remaining\":" + std::to_string(res.remaining) + "}";
    return resp;
}

net::HttpResponse HttpService::HandleQuotaInit(const net::HttpRequest& req)
{
    if (req.body.empty())
    {
        return BadRequest("missing JSON body");
    }

    std::string key;
    std::uint64_t quota = 0;
    bool has_quota = false;
    std::uint64_t ttl_ms = 0;

    try
    {
        protocol::JsonReader reader(req.body);
        reader.Expect('{');
        do
        {
            auto field = reader.String();
            reader.Expect(':');
            if (field == "key")
            {
                key = reader.String();
            }
            else if (field == "quota")
            {
                quota = reader.UnsignedNumber();
                has_quota = true;
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
                throw std::invalid_argument("unsupported field: " + field);
            }

            if (reader.Take('}'))
            {
                break;
            }
            reader.Expect(',');
        } while (true);
        reader.End();
    }
    catch (const std::exception& ex)
    {
        return BadRequest(std::string("invalid JSON: ") + ex.what());
    }

    if (key.empty() || !has_quota)
    {
        return BadRequest("key and quota are required");
    }

    const bool set = store_.Set(key, std::to_string(quota), storage::WriteCondition::Always, ttl_ms);
    if (aof_writer_)
    {
        std::string aof_body = "{\"key\":" + protocol::QuoteJson(key) +
                               ",\"amount\":" + std::to_string(quota) + "}";
        aof_writer_->Append("GK.QUOTA_INIT", aof_body);
    }

    net::HttpResponse resp;
    resp.status_code = 200;
    resp.status_text = "OK";
    resp.body = "{\"ok\":" + std::string(set ? "true" : "false") + ",\"quota\":" + std::to_string(quota) + "}";
    return resp;
}

net::HttpResponse HttpService::HandleIdempotencyBegin(const net::HttpRequest& req)
{
    if (req.body.empty())
    {
        return BadRequest("missing JSON body");
    }

    std::string key;
    std::string request_hash;
    std::uint64_t ttl_ms = 0;
    std::string owner_token;

    try
    {
        protocol::JsonReader reader(req.body);
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
                throw std::invalid_argument("unsupported field: " + field);
            }

            if (reader.Take('}'))
            {
                break;
            }
            reader.Expect(',');
        } while (true);
        reader.End();
    }
    catch (const std::exception& ex)
    {
        return BadRequest(std::string("invalid JSON: ") + ex.what());
    }

    if (key.empty() || request_hash.empty())
    {
        return BadRequest("key and request_hash are required");
    }

    const auto res = store_.IdemBegin(key, request_hash, ttl_ms, owner_token);
    if (!res.ok)
    {
        net::HttpResponse resp;
        if (res.action == storage::IdempotencyAction::Conflict)
        {
            resp.status_code = 409;
            resp.status_text = "Conflict";
        }
        else
        {
            resp.status_code = 400;
            resp.status_text = "Bad Request";
        }
        resp.body = "{\"error\":\"" + res.error_code + "\",\"message\":\"" + res.error_message + "\"}";
        return resp;
    }

    net::HttpResponse resp;
    resp.status_code = 200;
    resp.status_text = "OK";
    std::string body = "{\"action\":";
    switch (res.action)
    {
    case storage::IdempotencyAction::Execute:
        body += "\"EXECUTE\"";
        break;
    case storage::IdempotencyAction::Park:
        body += "\"PARK\"";
        break;
    case storage::IdempotencyAction::Replay:
        body += "\"REPLAY\"";
        break;
    default:
        body += "\"UNKNOWN\"";
        break;
    }
    body += ",\"owner_token\":" + protocol::QuoteJson(res.owner_token);
    if (res.action == storage::IdempotencyAction::Replay)
    {
        body += ",\"response_code\":" + std::to_string(res.cached_code);
        body += ",\"response_body\":" + protocol::QuoteJson(res.cached_response);
    }
    body += "}";
    if (aof_writer_ && res.action == storage::IdempotencyAction::Execute)
    {
        std::string aof_body = "{\"key\":" + protocol::QuoteJson(key) +
                               ",\"request_hash\":" + protocol::QuoteJson(request_hash) +
                               ",\"ttl_ms\":" + std::to_string(ttl_ms) +
                               ",\"owner_token\":" + protocol::QuoteJson(res.owner_token) + "}";
        aof_writer_->Append("GK.IDEM_BEGIN", aof_body);
    }
    resp.body = std::move(body);
    return resp;
}

net::HttpResponse HttpService::HandleIdempotencyComplete(const net::HttpRequest& req)
{
    if (req.body.empty())
    {
        return BadRequest("missing JSON body");
    }

    std::string key;
    std::string owner_token;
    int response_code = 200;
    std::string response_body;

    try
    {
        protocol::JsonReader reader(req.body);
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
                throw std::invalid_argument("unsupported field: " + field);
            }

            if (reader.Take('}'))
            {
                break;
            }
            reader.Expect(',');
        } while (true);
        reader.End();
    }
    catch (const std::exception& ex)
    {
        return BadRequest(std::string("invalid JSON: ") + ex.what());
    }

    if (key.empty() || owner_token.empty())
    {
        return BadRequest("key and owner_token are required");
    }

    const auto res = store_.IdemComplete(key, owner_token, response_code, response_body);
    if (!res.ok)
    {
        net::HttpResponse resp;
        if (res.error_code == "ERR_TOKEN_MISMATCH")
        {
            resp.status_code = 403;
            resp.status_text = "Forbidden";
        }
        else if (res.error_code == "ERR_NOT_FOUND" || res.error_code == "ERR_EXPIRED")
        {
            resp.status_code = 404;
            resp.status_text = "Not Found";
        }
        else
        {
            resp.status_code = 400;
            resp.status_text = "Bad Request";
        }
        resp.body = "{\"error\":\"" + res.error_code + "\",\"message\":\"" + res.error_message + "\"}";
        return resp;
    }

    net::HttpResponse resp;
    resp.status_code = 200;
    resp.status_text = "OK";
    resp.body = "{\"completed\":true}";
    if (aof_writer_)
    {
        std::string aof_body = "{\"key\":" + protocol::QuoteJson(key) +
                               ",\"owner_token\":" + protocol::QuoteJson(owner_token) +
                               ",\"response_code\":" + std::to_string(response_code) +
                               ",\"response_body\":" + protocol::QuoteJson(response_body) + "}";
        aof_writer_->Append("GK.IDEM_COMPLETE", aof_body);
    }
    return resp;
}

net::HttpResponse HttpService::HandleIdempotencyFail(const net::HttpRequest& req)
{
    if (req.body.empty())
    {
        return BadRequest("missing JSON body");
    }

    std::string key;
    std::string owner_token;
    std::string error_message;

    try
    {
        protocol::JsonReader reader(req.body);
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
                throw std::invalid_argument("unsupported field: " + field);
            }

            if (reader.Take('}'))
            {
                break;
            }
            reader.Expect(',');
        } while (true);
        reader.End();
    }
    catch (const std::exception& ex)
    {
        return BadRequest(std::string("invalid JSON: ") + ex.what());
    }

    if (key.empty() || owner_token.empty())
    {
        return BadRequest("key and owner_token are required");
    }

    const auto res = store_.IdemFail(key, owner_token, error_message);
    if (!res.ok)
    {
        net::HttpResponse resp;
        if (res.error_code == "ERR_TOKEN_MISMATCH")
        {
            resp.status_code = 403;
            resp.status_text = "Forbidden";
        }
        else if (res.error_code == "ERR_NOT_FOUND" || res.error_code == "ERR_EXPIRED")
        {
            resp.status_code = 404;
            resp.status_text = "Not Found";
        }
        else
        {
            resp.status_code = 400;
            resp.status_text = "Bad Request";
        }
        resp.body = "{\"error\":\"" + res.error_code + "\",\"message\":\"" + res.error_message + "\"}";
        return resp;
    }

    net::HttpResponse resp;
    resp.status_code = 200;
    resp.status_text = "OK";
    resp.body = "{\"failed\":true}";
    if (aof_writer_)
    {
        std::string aof_body = "{\"key\":" + protocol::QuoteJson(key) +
                               ",\"owner_token\":" + protocol::QuoteJson(owner_token) +
                               ",\"error_message\":" + protocol::QuoteJson(error_message) + "}";
        aof_writer_->Append("GK.IDEM_FAIL", aof_body);
    }
    return resp;
}

net::HttpResponse HttpService::HandleKvSet(const net::HttpRequest& req)
{
    if (req.body.empty())
    {
        return BadRequest("missing JSON body");
    }

    std::string key;
    std::string value;
    std::uint64_t ttl_ms = 0;
    storage::WriteCondition condition = storage::WriteCondition::Always;

    try
    {
        protocol::JsonReader reader(req.body);
        reader.Expect('{');
        do
        {
            auto field = reader.String();
            reader.Expect(':');
            if (field == "key")
            {
                key = reader.String();
            }
            else if (field == "value")
            {
                value = reader.String();
            }
            else if (field == "ttl_ms")
            {
                ttl_ms = reader.UnsignedNumber();
            }
            else if (field == "ttl_seconds")
            {
                ttl_ms = reader.UnsignedNumber() * 1000;
            }
            else if (field == "condition")
            {
                auto cond_str = reader.String();
                if (cond_str == "nx" || cond_str == "if_not_exists")
                {
                    condition = storage::WriteCondition::IfAbsent;
                }
                else if (cond_str == "xx" || cond_str == "if_exists")
                {
                    condition = storage::WriteCondition::IfPresent;
                }
                else if (cond_str == "always")
                {
                    condition = storage::WriteCondition::Always;
                }
                else
                {
                    throw std::invalid_argument("unsupported condition: " + cond_str);
                }
            }
            else if (field == "if_not_exists")
            {
                if (reader.Boolean())
                {
                    condition = storage::WriteCondition::IfAbsent;
                }
            }
            else if (field == "if_exists")
            {
                if (reader.Boolean())
                {
                    condition = storage::WriteCondition::IfPresent;
                }
            }
            else
            {
                throw std::invalid_argument("unsupported field: " + field);
            }

            if (reader.Take('}'))
            {
                break;
            }
            reader.Expect(',');
        } while (true);
        reader.End();
    }
    catch (const std::exception& ex)
    {
        return BadRequest(std::string("invalid JSON: ") + ex.what());
    }

    if (key.empty())
    {
        return BadRequest("key is required and must not be empty");
    }

    const bool stored = store_.Set(key, value, condition, ttl_ms);

    if (stored && aof_writer_)
    {
        std::string aof_body = "{\"key\":" + protocol::QuoteJson(key) +
                               ",\"value\":" + protocol::QuoteJson(value);
        if (ttl_ms > 0)
        {
            aof_body += ",\"ttl_ms\":" + std::to_string(ttl_ms);
        }
        if (condition == storage::WriteCondition::IfAbsent)
        {
            aof_body += ",\"if_not_exists\":true";
        }
        else if (condition == storage::WriteCondition::IfPresent)
        {
            aof_body += ",\"if_exists\":true";
        }
        aof_body += "}";
        aof_writer_->Append("SET", aof_body);
    }

    net::HttpResponse resp;
    resp.status_code = 200;
    resp.status_text = "OK";
    resp.body = std::string("{\"ok\":true,\"set\":") + (stored ? "true}" : "false}");
    return resp;
}

net::HttpResponse HttpService::HandleKvGet(const net::HttpRequest& req)
{
    std::string key = ExtractKeyFromQuery(req.query);

    if (key.empty() && !req.body.empty())
    {
        try
        {
            protocol::JsonReader reader(req.body);
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
                    throw std::invalid_argument("unsupported field: " + field);
                }
                if (reader.Take('}'))
                {
                    break;
                }
                reader.Expect(',');
            } while (true);
            reader.End();
        }
        catch (const std::exception& ex)
        {
            return BadRequest(std::string("invalid JSON: ") + ex.what());
        }
    }

    if (key.empty())
    {
        return BadRequest("key is required");
    }

    const auto val = store_.Get(key);
    if (!val)
    {
        net::HttpResponse resp;
        resp.status_code = 404;
        resp.status_text = "Not Found";
        resp.body = "{\"error\":\"KEY_NOT_FOUND\",\"message\":\"key does not exist\"}";
        return resp;
    }

    net::HttpResponse resp;
    resp.status_code = 200;
    resp.status_text = "OK";
    resp.body = "{\"key\":" + protocol::QuoteJson(key) + ",\"value\":" + protocol::QuoteJson(*val) + "}";
    return resp;
}

net::HttpResponse HttpService::HandleKvDel(const net::HttpRequest& req)
{
    std::vector<std::string> keys;

    if (!req.body.empty())
    {
        try
        {
            protocol::JsonReader reader(req.body);
            reader.Expect('{');
            if (!reader.Take('}'))
            {
                do
                {
                    auto field = reader.String();
                    reader.Expect(':');
                    if (field == "key")
                    {
                        keys.push_back(reader.String());
                    }
                    else if (field == "keys")
                    {
                        auto arr = reader.StringArray();
                        keys.insert(keys.end(), arr.begin(), arr.end());
                    }
                    else
                    {
                        throw std::invalid_argument("unsupported field: " + field);
                    }
                    if (reader.Take('}'))
                    {
                        break;
                    }
                    reader.Expect(',');
                } while (true);
            }
            reader.End();
        }
        catch (const std::exception& ex)
        {
            return BadRequest(std::string("invalid JSON: ") + ex.what());
        }
    }
    else
    {
        std::string key = ExtractKeyFromQuery(req.query);
        if (!key.empty())
        {
            keys.push_back(std::move(key));
        }
    }

    if (keys.empty())
    {
        return BadRequest("key or keys are required");
    }

    std::size_t deleted = 0;
    for (const auto& k : keys)
    {
        if (store_.Del(k))
        {
            ++deleted;
            if (aof_writer_)
            {
                aof_writer_->Append("DEL", "{\"key\":" + protocol::QuoteJson(k) + "}");
            }
        }
    }

    net::HttpResponse resp;
    resp.status_code = 200;
    resp.status_text = "OK";
    resp.body = "{\"deleted\":" + std::to_string(deleted) + "}";
    return resp;
}

net::HttpResponse HttpService::HandleKvExists(const net::HttpRequest& req)
{
    std::vector<std::string> keys;

    if (!req.body.empty())
    {
        try
        {
            protocol::JsonReader reader(req.body);
            reader.Expect('{');
            if (!reader.Take('}'))
            {
                do
                {
                    auto field = reader.String();
                    reader.Expect(':');
                    if (field == "key")
                    {
                        keys.push_back(reader.String());
                    }
                    else if (field == "keys")
                    {
                        auto arr = reader.StringArray();
                        keys.insert(keys.end(), arr.begin(), arr.end());
                    }
                    else
                    {
                        throw std::invalid_argument("unsupported field: " + field);
                    }
                    if (reader.Take('}'))
                    {
                        break;
                    }
                    reader.Expect(',');
                } while (true);
            }
            reader.End();
        }
        catch (const std::exception& ex)
        {
            return BadRequest(std::string("invalid JSON: ") + ex.what());
        }
    }
    else
    {
        std::string key = ExtractKeyFromQuery(req.query);
        if (!key.empty())
        {
            keys.push_back(std::move(key));
        }
    }

    if (keys.empty())
    {
        return BadRequest("key or keys are required");
    }

    std::size_t count = 0;
    for (const auto& k : keys)
    {
        if (store_.Exists(k))
        {
            ++count;
        }
    }

    net::HttpResponse resp;
    resp.status_code = 200;
    resp.status_text = "OK";
    resp.body = "{\"count\":" + std::to_string(count) + "}";
    return resp;
}

net::HttpResponse HttpService::HandleKvType(const net::HttpRequest& req)
{
    std::string key = ExtractKeyFromQuery(req.query);

    if (key.empty() && !req.body.empty())
    {
        try
        {
            protocol::JsonReader reader(req.body);
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
                    throw std::invalid_argument("unsupported field: " + field);
                }
                if (reader.Take('}'))
                {
                    break;
                }
                reader.Expect(',');
            } while (true);
            reader.End();
        }
        catch (const std::exception& ex)
        {
            return BadRequest(std::string("invalid JSON: ") + ex.what());
        }
    }

    if (key.empty())
    {
        return BadRequest("key is required");
    }

    const auto type = store_.Type(key);
    std::string type_str = "none";
    if (type == storage::DataType::String)
    {
        type_str = "string";
    }

    net::HttpResponse resp;
    resp.status_code = 200;
    resp.status_text = "OK";
    resp.body = "{\"key\":" + protocol::QuoteJson(key) + ",\"type\":\"" + type_str + "\"}";
    return resp;
}

net::HttpResponse HttpService::HandleKvExpire(const net::HttpRequest& req)
{
    if (req.body.empty())
    {
        return BadRequest("missing JSON body");
    }

    std::string key;
    std::uint64_t ttl_ms = 0;
    bool has_ttl = false;

    try
    {
        protocol::JsonReader reader(req.body);
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
                ttl_ms = reader.UnsignedNumber();
                has_ttl = true;
            }
            else if (field == "ttl_seconds" || field == "seconds")
            {
                ttl_ms = reader.UnsignedNumber() * 1000;
                has_ttl = true;
            }
            else
            {
                throw std::invalid_argument("unsupported field: " + field);
            }

            if (reader.Take('}'))
            {
                break;
            }
            reader.Expect(',');
        } while (true);
        reader.End();
    }
    catch (const std::exception& ex)
    {
        return BadRequest(std::string("invalid JSON: ") + ex.what());
    }

    if (key.empty() || !has_ttl)
    {
        return BadRequest("key and ttl are required");
    }

    const bool ok = store_.Expire(key, ttl_ms);
    if (ok && aof_writer_)
    {
        aof_writer_->Append("PEXPIRE", "{\"key\":" + protocol::QuoteJson(key) + ",\"ttl_ms\":" + std::to_string(ttl_ms) + "}");
    }

    net::HttpResponse resp;
    resp.status_code = 200;
    resp.status_text = "OK";
    resp.body = std::string("{\"ok\":") + (ok ? "true}" : "false}");
    return resp;
}

net::HttpResponse HttpService::HandleKvTtl(const net::HttpRequest& req)
{
    std::string key = ExtractKeyFromQuery(req.query);

    if (key.empty() && !req.body.empty())
    {
        try
        {
            protocol::JsonReader reader(req.body);
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
                    throw std::invalid_argument("unsupported field: " + field);
                }
                if (reader.Take('}'))
                {
                    break;
                }
                reader.Expect(',');
            } while (true);
            reader.End();
        }
        catch (const std::exception& ex)
        {
            return BadRequest(std::string("invalid JSON: ") + ex.what());
        }
    }

    if (key.empty())
    {
        return BadRequest("key is required");
    }

    const auto ttl_sec = store_.Ttl(key);
    const auto ttl_ms = store_.Pttl(key);

    net::HttpResponse resp;
    resp.status_code = 200;
    resp.status_text = "OK";
    resp.body = "{\"key\":" + protocol::QuoteJson(key) + ",\"ttl_seconds\":" + std::to_string(ttl_sec) + ",\"ttl_ms\":" + std::to_string(ttl_ms) + "}";
    return resp;
}

net::HttpResponse HttpService::BadRequest(const std::string& message)
{
    net::HttpResponse resp;
    resp.status_code = 400;
    resp.status_text = "Bad Request";
    resp.body = "{\"error\":\"INVALID_ARGUMENTS\",\"message\":\"" + message + "\"}";
    return resp;
}

}
