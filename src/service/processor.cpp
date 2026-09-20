#include "gatekeeper/service/processor.h"
#include "gatekeeper/protocol/json_parser.h"
#include "gatekeeper/protocol/response.h"
#include "gatekeeper/protocol/gkwp2/binary_codec.h"
#include "gatekeeper/storage/store.h"

#include <arpa/inet.h>
#include <cstring>
#include <endian.h>
#include <stdexcept>
#include <utility>

namespace gatekeeper::service
{

Processor::Processor(Dispatch dispatch, std::shared_ptr<log::Logger> logger, storage::Store* store)
    : dispatch_(std::move(dispatch)), logger_(std::move(logger)), store_(store)
{
    if (!logger_)
    {
        logger_ = log::Logger::Null();
    }
    if (!dispatch_)
    {
        throw std::invalid_argument("request dispatcher is required");
    }
}

std::string Processor::ProcessBinary(std::string_view payload) const
{
    if (payload.size() < 2 || !store_)
    {
        return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
    }

    std::uint16_t net_opcode;
    std::memcpy(&net_opcode, payload.data(), 2);
    const auto opcode = static_cast<protocol::gkwp2::BinaryOpcode>(ntohs(net_opcode));
    payload.remove_prefix(2);

    switch (opcode)
    {
    case protocol::gkwp2::BinaryOpcode::Get:
    {
        if (payload.size() < 2) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        std::uint16_t key_len;
        std::memcpy(&key_len, payload.data(), 2);
        key_len = ntohs(key_len);
        payload.remove_prefix(2);
        if (payload.size() < key_len) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        const auto key = payload.substr(0, key_len);

        const auto val_opt = store_->Get(key);
        if (!val_opt.has_value())
        {
            return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::NotFound));
        }
        std::string resp;
        resp.reserve(1 + 4 + val_opt->size());
        resp.push_back(static_cast<char>(protocol::gkwp2::BinaryStatus::Ok));
        std::uint32_t val_len = htonl(static_cast<std::uint32_t>(val_opt->size()));
        resp.append(reinterpret_cast<const char*>(&val_len), 4);
        resp.append(*val_opt);
        return resp;
    }
    case protocol::gkwp2::BinaryOpcode::Set:
    {
        if (payload.size() < 2) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        std::uint16_t key_len;
        std::memcpy(&key_len, payload.data(), 2);
        key_len = ntohs(key_len);
        payload.remove_prefix(2);
        if (payload.size() < key_len + 4) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        const auto key = payload.substr(0, key_len);
        payload.remove_prefix(key_len);

        std::uint32_t val_len;
        std::memcpy(&val_len, payload.data(), 4);
        val_len = ntohl(val_len);
        payload.remove_prefix(4);
        if (payload.size() < val_len + 8 + 1) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        const auto val = payload.substr(0, val_len);
        payload.remove_prefix(val_len);

        std::uint64_t ttl_ms;
        std::memcpy(&ttl_ms, payload.data(), 8);
        ttl_ms = be64toh(ttl_ms);
        payload.remove_prefix(8);

        const auto cond_byte = static_cast<std::uint8_t>(payload[0]);
        auto cond = storage::WriteCondition::Always;
        if (cond_byte == 1) cond = storage::WriteCondition::IfAbsent;
        else if (cond_byte == 2) cond = storage::WriteCondition::IfPresent;

        const bool stored = store_->Set(std::string(key), std::string(val), cond, ttl_ms);
        if (!stored)
        {
            return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::NotFound));
        }
        return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Ok));
    }
    case protocol::gkwp2::BinaryOpcode::RateLimit:
    {
        if (payload.size() < 2) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        std::uint16_t key_len;
        std::memcpy(&key_len, payload.data(), 2);
        key_len = ntohs(key_len);
        payload.remove_prefix(2);
        if (payload.size() < key_len + 8 + 8 + 4) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        const auto key = payload.substr(0, key_len);
        payload.remove_prefix(key_len);

        std::uint64_t limit, window_ms;
        std::uint32_t cost;
        std::memcpy(&limit, payload.data(), 8); limit = be64toh(limit); payload.remove_prefix(8);
        std::memcpy(&window_ms, payload.data(), 8); window_ms = be64toh(window_ms); payload.remove_prefix(8);
        std::memcpy(&cost, payload.data(), 4); cost = ntohl(cost);

        const auto res = store_->RateLimit(key, limit, window_ms, cost);
        if (!res.ok)
        {
            return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        }

        std::string resp;
        resp.reserve(1 + 1 + 8 + 8);
        resp.push_back(static_cast<char>(protocol::gkwp2::BinaryStatus::Ok));
        resp.push_back(res.allowed ? 1 : 0);
        const std::uint64_t remaining_net = htobe64(res.remaining);
        resp.append(reinterpret_cast<const char*>(&remaining_net), 8);
        const std::uint64_t retry_net = htobe64(res.retry_after_ms);
        resp.append(reinterpret_cast<const char*>(&retry_net), 8);
        return resp;
    }
    case protocol::gkwp2::BinaryOpcode::Incr:
    {
        if (payload.size() < 2) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        std::uint16_t key_len;
        std::memcpy(&key_len, payload.data(), 2);
        key_len = ntohs(key_len);
        payload.remove_prefix(2);
        if (payload.size() < key_len + 8 + 8) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        const auto key = payload.substr(0, key_len);
        payload.remove_prefix(key_len);

        std::int64_t delta;
        std::uint64_t init_ttl_ms;
        std::memcpy(&delta, payload.data(), 8); delta = be64toh(delta); payload.remove_prefix(8);
        std::memcpy(&init_ttl_ms, payload.data(), 8); init_ttl_ms = be64toh(init_ttl_ms);

        const auto res = store_->IncrBy(key, delta, init_ttl_ms);
        if (!res.ok)
        {
            return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        }
        std::string resp;
        resp.reserve(1 + 8);
        resp.push_back(static_cast<char>(protocol::gkwp2::BinaryStatus::Ok));
        const std::int64_t val_net = htobe64(res.value);
        resp.append(reinterpret_cast<const char*>(&val_net), 8);
        return resp;
    }
    default:
        return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
    }
}

std::string Processor::Process(std::string_view payload) const
{
    if (!payload.empty() && payload.front() != '{')
    {
        return ProcessBinary(payload);
    }

    protocol::Request request;
    protocol::Error error;
    const protocol::Parser parser;
    if (!parser.Parse(payload, request, error))
    {
        const auto err_response = protocol::EncodeErrorResponse(request.id, error);
        logger_->Warn("Failed to parse request: " + error.code + " - " + error.message);
        logger_->Info("Response GKWP: " + err_response);
        return err_response;
    }
    logger_->Info("Executing request id=\"" + request.id + "\" op=\"" + request.op + "\"");
    const auto result = dispatch_(request);
    std::string response;
    if (result.ok)
    {
        response = protocol::EncodeSuccessResponse(request.id, result.result_json);
        logger_->Info("Response GKWP: " + response);
    }
    else
    {
        response = protocol::EncodeErrorResponse(request.id, result.error);
        logger_->Warn("Response GKWP: " + response);
    }
    return response;
}

}
