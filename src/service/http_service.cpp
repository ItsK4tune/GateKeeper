#include "gatekeeper/service/http_service.h"
#include "gatekeeper/protocol/gkwp/json_reader.h"
#include "gatekeeper/protocol/gkwp/response.h"

#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>

namespace gatekeeper::service
{

HttpService::HttpService(storage::Store& store, std::shared_ptr<log::Logger> logger)
    : store_(store), logger_(std::move(logger))
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

    if (req.path == "/healthz" || req.path == "/v1/rate-limit/check" ||
        req.path == "/v1/quota/reserve" || req.path == "/v1/quota/commit" ||
        req.path == "/v1/quota/rollback" || req.path == "/v1/quota/init")
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

    if (key.empty() || !has_amount || amount == 0 || !has_ttl || ttl_ms == 0)
    {
        return BadRequest("key, amount (>0) and ttl_ms (>0) are required");
    }

    const auto res = store_.ReserveQuota(key, amount, ttl_ms);
    if (!res.ok)
    {
        return BadRequest(res.error_message);
    }

    net::HttpResponse resp;
    if (res.reserved)
    {
        resp.status_code = 200;
        resp.status_text = "OK";
        resp.body = "{\"reserved\":true,\"reservation_id\":\"" + res.reservation_id +
                    "\",\"remaining\":" + std::to_string(res.remaining) + "}";
    }
    else
    {
        resp.status_code = 429;
        resp.status_text = "Too Many Requests";
        resp.body = "{\"reserved\":false,\"reservation_id\":\"\",\"remaining\":" +
                    std::to_string(res.remaining) + "}";
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
    std::string reservation_id;
    std::uint64_t actual_amount = 0;

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
                reservation_id = reader.String();
            }
            else if (field == "actual_amount")
            {
                actual_amount = reader.UnsignedNumber();
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

    if (key.empty() || reservation_id.empty())
    {
        return BadRequest("key and reservation_id are required");
    }

    const auto res = store_.CommitQuota(key, reservation_id, actual_amount);
    if (!res.ok)
    {
        return BadRequest(res.error_message);
    }

    net::HttpResponse resp;
    if (res.committed)
    {
        resp.status_code = 200;
        resp.status_text = "OK";
        resp.body = "{\"committed\":true,\"actual_amount\":" + std::to_string(res.actual_amount) +
                    ",\"refunded\":" + std::to_string(res.refunded) +
                    ",\"remaining\":" + std::to_string(res.remaining) + "}";
    }
    else
    {
        resp.status_code = 404;
        resp.status_text = "Not Found";
        resp.body = "{\"committed\":false,\"error\":\"" + res.error_code +
                    "\",\"message\":\"" + res.error_message + "\"}";
    }
    return resp;
}

net::HttpResponse HttpService::HandleQuotaRollback(const net::HttpRequest& req)
{
    if (req.body.empty())
    {
        return BadRequest("missing JSON body");
    }

    std::string key;
    std::string reservation_id;

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
                reservation_id = reader.String();
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

    if (key.empty() || reservation_id.empty())
    {
        return BadRequest("key and reservation_id are required");
    }

    const auto res = store_.RollbackQuota(key, reservation_id);
    if (!res.ok)
    {
        return BadRequest(res.error_message);
    }

    net::HttpResponse resp;
    if (res.rolled_back)
    {
        resp.status_code = 200;
        resp.status_text = "OK";
        resp.body = "{\"rolled_back\":true,\"refunded\":" + std::to_string(res.refunded) +
                    ",\"remaining\":" + std::to_string(res.remaining) + "}";
    }
    else
    {
        resp.status_code = 404;
        resp.status_text = "Not Found";
        resp.body = "{\"rolled_back\":false,\"error\":\"" + res.error_code +
                    "\",\"message\":\"" + res.error_message + "\"}";
    }
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

    store_.Set(key, std::to_string(quota), storage::WriteCondition::Always, ttl_ms);

    net::HttpResponse resp;
    resp.status_code = 200;
    resp.status_text = "OK";
    resp.body = "{\"ok\":true,\"quota\":" + std::to_string(quota) + "}";
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
