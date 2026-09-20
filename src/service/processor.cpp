#include "gatekeeper/domain/lock/lock_wait_queue.h"
#include "gatekeeper/service/processor.h"
#include "gatekeeper/protocol/gkwp2/binary_codec.h"
#include "gatekeeper/protocol/response.h"

#include <arpa/inet.h>
#include <cstring>
#include <endian.h>
#include <stdexcept>
#include <utility>

namespace gatekeeper::service
{

Processor::Processor(storage::Store* store,
                     std::shared_ptr<log::Logger> logger,
                     std::shared_ptr<storage::aof::AofWriter> aof_writer)
    : store_(store), logger_(std::move(logger)), aof_writer_(std::move(aof_writer))
{
    if (!logger_)
    {
        logger_ = log::Logger::Null();
    }
    if (!store_)
    {
        throw std::invalid_argument("storage store is required");
    }
}

std::string Processor::Process(std::string_view payload) const
{
    if (payload.size() < 2)
    {
        return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
    }

    std::uint16_t net_opcode;
    std::memcpy(&net_opcode, payload.data(), 2);
    const auto opcode = static_cast<protocol::gkwp2::BinaryOpcode>(ntohs(net_opcode));
    payload.remove_prefix(2);

    switch (opcode)
    {
    case protocol::gkwp2::BinaryOpcode::Ping:
    {
        std::string resp;
        resp.push_back(static_cast<char>(protocol::gkwp2::BinaryStatus::Ok));
        std::uint16_t len = htons(4);
        resp.append(reinterpret_cast<const char*>(&len), 2);
        resp.append("PONG");
        return resp;
    }
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
        if (aof_writer_)
        {
            std::string aof = "{\"key\":" + protocol::QuoteJson(key) + ",\"value\":" + protocol::QuoteJson(val);
            if (ttl_ms > 0) aof += ",\"ttl_ms\":" + std::to_string(ttl_ms);
            if (cond == storage::WriteCondition::IfAbsent) aof += ",\"if_not_exists\":true";
            else if (cond == storage::WriteCondition::IfPresent) aof += ",\"if_exists\":true";
            aof += "}";
            aof_writer_->Append("SET", aof);
        }
        return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Ok));
    }
    case protocol::gkwp2::BinaryOpcode::Del:
    {
        if (payload.size() < 2) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        std::uint16_t key_len;
        std::memcpy(&key_len, payload.data(), 2);
        key_len = ntohs(key_len);
        payload.remove_prefix(2);
        if (payload.size() < key_len) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        const auto key = payload.substr(0, key_len);

        const bool deleted = store_->Del(key);
        if (deleted && aof_writer_)
        {
            aof_writer_->Append("DEL", "{\"key\":" + protocol::QuoteJson(key) + "}");
        }
        std::string resp;
        resp.push_back(static_cast<char>(protocol::gkwp2::BinaryStatus::Ok));
        resp.push_back(deleted ? 1 : 0);
        return resp;
    }
    case protocol::gkwp2::BinaryOpcode::Exists:
    {
        if (payload.size() < 2) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        std::uint16_t key_len;
        std::memcpy(&key_len, payload.data(), 2);
        key_len = ntohs(key_len);
        payload.remove_prefix(2);
        if (payload.size() < key_len) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        const auto key = payload.substr(0, key_len);

        const bool exists = store_->Exists(key);
        std::string resp;
        resp.push_back(static_cast<char>(protocol::gkwp2::BinaryStatus::Ok));
        resp.push_back(exists ? 1 : 0);
        return resp;
    }
    case protocol::gkwp2::BinaryOpcode::Type:
    {
        if (payload.size() < 2) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        std::uint16_t key_len;
        std::memcpy(&key_len, payload.data(), 2);
        key_len = ntohs(key_len);
        payload.remove_prefix(2);
        if (payload.size() < key_len) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        const auto key = payload.substr(0, key_len);

        const auto type = store_->Type(key);
        std::string type_name = "none";
        if (type == storage::DataType::String) type_name = "string";

        std::string resp;
        resp.push_back(static_cast<char>(protocol::gkwp2::BinaryStatus::Ok));
        std::uint16_t tlen = htons(static_cast<std::uint16_t>(type_name.size()));
        resp.append(reinterpret_cast<const char*>(&tlen), 2);
        resp.append(type_name);
        return resp;
    }
    case protocol::gkwp2::BinaryOpcode::Expire:
    {
        if (payload.size() < 2 + 8) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        std::uint16_t key_len;
        std::memcpy(&key_len, payload.data(), 2);
        key_len = ntohs(key_len);
        payload.remove_prefix(2);
        if (payload.size() < key_len + 8) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        const auto key = payload.substr(0, key_len);
        payload.remove_prefix(key_len);

        std::uint64_t ttl_ms;
        std::memcpy(&ttl_ms, payload.data(), 8);
        ttl_ms = be64toh(ttl_ms);

        const bool success = store_->Expire(key, ttl_ms);
        if (success && aof_writer_)
        {
            aof_writer_->Append("PEXPIRE", "{\"key\":" + protocol::QuoteJson(key) + ",\"ttl_ms\":" + std::to_string(ttl_ms) + "}");
        }
        std::string resp;
        resp.push_back(static_cast<char>(protocol::gkwp2::BinaryStatus::Ok));
        resp.push_back(success ? 1 : 0);
        return resp;
    }
    case protocol::gkwp2::BinaryOpcode::Ttl:
    {
        if (payload.size() < 2) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        std::uint16_t key_len;
        std::memcpy(&key_len, payload.data(), 2);
        key_len = ntohs(key_len);
        payload.remove_prefix(2);
        if (payload.size() < key_len) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        const auto key = payload.substr(0, key_len);

        const auto ttl_ms = store_->Ttl(key);

        std::string resp;
        resp.push_back(static_cast<char>(protocol::gkwp2::BinaryStatus::Ok));
        std::uint64_t ttl_net = htobe64(static_cast<std::uint64_t>(ttl_ms));
        resp.append(reinterpret_cast<const char*>(&ttl_net), 8);
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
        if (aof_writer_)
        {
            std::string aof = "{\"key\":" + protocol::QuoteJson(key) + ",\"delta\":" + std::to_string(delta);
            if (init_ttl_ms > 0) aof += ",\"ttl_ms\":" + std::to_string(init_ttl_ms);
            aof += "}";
            aof_writer_->Append("INCRBY", aof);
        }
        std::string resp;
        resp.reserve(1 + 8);
        resp.push_back(static_cast<char>(protocol::gkwp2::BinaryStatus::Ok));
        const std::int64_t val_net = htobe64(res.value);
        resp.append(reinterpret_cast<const char*>(&val_net), 8);
        return resp;
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
    case protocol::gkwp2::BinaryOpcode::QuotaInit:
    {
        if (payload.size() < 2 + 8) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        std::uint16_t key_len;
        std::memcpy(&key_len, payload.data(), 2);
        key_len = ntohs(key_len);
        payload.remove_prefix(2);
        if (payload.size() < key_len + 8) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        const auto key = payload.substr(0, key_len);
        payload.remove_prefix(key_len);

        std::uint64_t amount;
        std::memcpy(&amount, payload.data(), 8);
        amount = be64toh(amount);

        store_->Set(std::string(key), std::to_string(amount), storage::WriteCondition::Always, 0);
        if (aof_writer_)
        {
            aof_writer_->Append("GK.QUOTA_INIT", "{\"key\":" + protocol::QuoteJson(key) + ",\"amount\":" + std::to_string(amount) + "}");
        }
        return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Ok));
    }
    case protocol::gkwp2::BinaryOpcode::QuotaGet:
    {
        if (payload.size() < 2) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        std::uint16_t key_len;
        std::memcpy(&key_len, payload.data(), 2);
        key_len = ntohs(key_len);
        payload.remove_prefix(2);
        if (payload.size() < key_len) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        const auto key = payload.substr(0, key_len);

        const auto val_opt = store_->Get(key);
        std::uint64_t balance = 0;
        if (val_opt.has_value())
        {
            try { balance = std::stoull(*val_opt); } catch (...) {}
        }
        std::string resp;
        resp.push_back(static_cast<char>(protocol::gkwp2::BinaryStatus::Ok));
        std::uint64_t bal_net = htobe64(balance);
        resp.append(reinterpret_cast<const char*>(&bal_net), 8);
        return resp;
    }
    case protocol::gkwp2::BinaryOpcode::Reserve:
    {
        if (payload.size() < 2) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        std::uint16_t key_len;
        std::memcpy(&key_len, payload.data(), 2);
        key_len = ntohs(key_len);
        payload.remove_prefix(2);
        if (payload.size() < key_len + 8 + 8) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        const auto key = payload.substr(0, key_len);
        payload.remove_prefix(key_len);

        std::uint64_t amount, ttl_ms;
        std::memcpy(&amount, payload.data(), 8); amount = be64toh(amount); payload.remove_prefix(8);
        std::memcpy(&ttl_ms, payload.data(), 8); ttl_ms = be64toh(ttl_ms);

        const auto res = store_->ReserveQuota(key, amount, ttl_ms);
        if (!res.ok)
        {
            return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        }

        if (res.reserved && aof_writer_)
        {
            aof_writer_->Append("GK.RESERVE", "{\"key\":\"" + std::string(key) + "\",\"amount\":" + std::to_string(amount) +
                ",\"ttl_ms\":" + std::to_string(ttl_ms) + ",\"reservation_id\":\"" + res.reservation_id + "\"}");
        }

        std::string resp;
        resp.push_back(static_cast<char>(protocol::gkwp2::BinaryStatus::Ok));
        resp.push_back(res.reserved ? 1 : 0);
        std::uint64_t rem_net = htobe64(res.remaining);
        resp.append(reinterpret_cast<const char*>(&rem_net), 8);
        std::uint16_t rlen = htons(static_cast<std::uint16_t>(res.reservation_id.size()));
        resp.append(reinterpret_cast<const char*>(&rlen), 2);
        resp.append(res.reservation_id);
        return resp;
    }
    case protocol::gkwp2::BinaryOpcode::Commit:
    {
        if (payload.size() < 2) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        std::uint16_t key_len;
        std::memcpy(&key_len, payload.data(), 2);
        key_len = ntohs(key_len);
        payload.remove_prefix(2);
        if (payload.size() < key_len + 2) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        const auto key = payload.substr(0, key_len);
        payload.remove_prefix(key_len);

        std::uint16_t rlen;
        std::memcpy(&rlen, payload.data(), 2); rlen = ntohs(rlen); payload.remove_prefix(2);
        if (payload.size() < rlen + 8) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        const auto res_id = payload.substr(0, rlen);
        payload.remove_prefix(rlen);

        std::uint64_t actual_amount;
        std::memcpy(&actual_amount, payload.data(), 8); actual_amount = be64toh(actual_amount);

        const auto res = store_->CommitQuota(key, res_id, actual_amount);
        if (!res.ok)
        {
            return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        }

        if (aof_writer_)
        {
            aof_writer_->Append("GK.COMMIT", "{\"key\":\"" + std::string(key) + "\",\"reservation_id\":\"" + std::string(res_id) +
                "\",\"actual_amount\":" + std::to_string(actual_amount) + "}");
        }

        std::string resp;
        resp.push_back(static_cast<char>(protocol::gkwp2::BinaryStatus::Ok));
        resp.push_back(res.committed ? 1 : 0);
        std::uint64_t ref_net = htobe64(res.refunded);
        resp.append(reinterpret_cast<const char*>(&ref_net), 8);
        std::uint64_t bal_net = htobe64(res.remaining);
        resp.append(reinterpret_cast<const char*>(&bal_net), 8);
        return resp;
    }
    case protocol::gkwp2::BinaryOpcode::Rollback:
    {
        if (payload.size() < 2) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        std::uint16_t key_len;
        std::memcpy(&key_len, payload.data(), 2);
        key_len = ntohs(key_len);
        payload.remove_prefix(2);
        if (payload.size() < key_len + 2) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        const auto key = payload.substr(0, key_len);
        payload.remove_prefix(key_len);

        std::uint16_t rlen;
        std::memcpy(&rlen, payload.data(), 2); rlen = ntohs(rlen); payload.remove_prefix(2);
        if (payload.size() < rlen) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        const auto res_id = payload.substr(0, rlen);

        const auto res = store_->RollbackQuota(key, res_id);
        if (!res.ok)
        {
            return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        }

        if (aof_writer_)
        {
            aof_writer_->Append("GK.ROLLBACK", "{\"key\":" + protocol::QuoteJson(key) + ",\"reservation_id\":" + protocol::QuoteJson(res_id) + "}");
        }

        std::string resp;
        resp.push_back(static_cast<char>(protocol::gkwp2::BinaryStatus::Ok));
        resp.push_back(res.rolled_back ? 1 : 0);
        std::uint64_t ref_net = htobe64(res.refunded);
        resp.append(reinterpret_cast<const char*>(&ref_net), 8);
        std::uint64_t bal_net = htobe64(res.remaining);
        resp.append(reinterpret_cast<const char*>(&bal_net), 8);
        return resp;
    }
    case protocol::gkwp2::BinaryOpcode::IdemBegin:
    {
        if (payload.size() < 2) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        std::uint16_t key_len;
        std::memcpy(&key_len, payload.data(), 2); key_len = ntohs(key_len); payload.remove_prefix(2);
        if (payload.size() < key_len + 2) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        const auto key = payload.substr(0, key_len); payload.remove_prefix(key_len);

        std::uint16_t hlen;
        std::memcpy(&hlen, payload.data(), 2); hlen = ntohs(hlen); payload.remove_prefix(2);
        if (payload.size() < hlen + 8 + 2) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        const auto req_hash = payload.substr(0, hlen); payload.remove_prefix(hlen);

        std::uint64_t ttl_ms;
        std::memcpy(&ttl_ms, payload.data(), 8); ttl_ms = be64toh(ttl_ms); payload.remove_prefix(8);

        std::uint16_t olen;
        std::memcpy(&olen, payload.data(), 2); olen = ntohs(olen); payload.remove_prefix(2);
        std::string_view owner_token;
        if (olen > 0 && payload.size() >= olen)
        {
            owner_token = payload.substr(0, olen);
        }

        const auto res = store_->IdemBegin(key, req_hash, ttl_ms, owner_token);
        if (!res.ok)
        {
            return std::string(1, static_cast<char>(res.action == storage::IdempotencyAction::Conflict
                ? protocol::gkwp2::BinaryStatus::Conflict
                : protocol::gkwp2::BinaryStatus::Error));
        }

        if (aof_writer_ && res.action == storage::IdempotencyAction::Execute)
        {
            aof_writer_->Append("GK.IDEM_BEGIN", "{\"key\":\"" + std::string(key) + "\",\"request_hash\":\"" + std::string(req_hash) +
                "\",\"ttl_ms\":" + std::to_string(ttl_ms) + ",\"owner_token\":\"" + res.owner_token + "\"}");
        }

        std::string resp;
        resp.push_back(static_cast<char>(protocol::gkwp2::BinaryStatus::Ok));
        std::uint8_t action_byte = 0;
        if (res.action == storage::IdempotencyAction::Park) action_byte = 1;
        else if (res.action == storage::IdempotencyAction::Replay) action_byte = 2;
        else if (res.action == storage::IdempotencyAction::Conflict) action_byte = 3;
        resp.push_back(static_cast<char>(action_byte));

        std::uint16_t tok_len = htons(static_cast<std::uint16_t>(res.owner_token.size()));
        resp.append(reinterpret_cast<const char*>(&tok_len), 2);
        resp.append(res.owner_token);

        std::uint16_t code_net = htons(static_cast<std::uint16_t>(res.cached_code));
        resp.append(reinterpret_cast<const char*>(&code_net), 2);
        std::uint32_t clen_net = htonl(static_cast<std::uint32_t>(res.cached_response.size()));
        resp.append(reinterpret_cast<const char*>(&clen_net), 4);
        resp.append(res.cached_response);
        return resp;
    }
    case protocol::gkwp2::BinaryOpcode::IdemComplete:
    {
        if (payload.size() < 2) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        std::uint16_t key_len;
        std::memcpy(&key_len, payload.data(), 2); key_len = ntohs(key_len); payload.remove_prefix(2);
        if (payload.size() < key_len + 2) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        const auto key = payload.substr(0, key_len); payload.remove_prefix(key_len);

        std::uint16_t olen;
        std::memcpy(&olen, payload.data(), 2); olen = ntohs(olen); payload.remove_prefix(2);
        if (payload.size() < olen + 2 + 4) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        const auto owner_token = payload.substr(0, olen); payload.remove_prefix(olen);

        std::uint16_t code_net;
        std::memcpy(&code_net, payload.data(), 2); int code = ntohs(code_net); payload.remove_prefix(2);

        std::uint32_t blen_net;
        std::memcpy(&blen_net, payload.data(), 4); std::uint32_t blen = ntohl(blen_net); payload.remove_prefix(4);
        if (payload.size() < blen) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        const auto body = payload.substr(0, blen);

        const auto res = store_->IdemComplete(key, owner_token, code, body);
        if (!res.ok)
        {
            return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        }

        if (aof_writer_)
        {
            aof_writer_->Append("GK.IDEM_COMPLETE", "{\"key\":\"" + std::string(key) + "\",\"owner_token\":\"" + std::string(owner_token) +
                "\",\"response_code\":" + std::to_string(code) + ",\"response_body\":\"" + std::string(body) + "\"}");
        }

        std::string resp;
        resp.push_back(static_cast<char>(protocol::gkwp2::BinaryStatus::Ok));
        resp.push_back(1);
        return resp;
    }
    case protocol::gkwp2::BinaryOpcode::IdemFail:
    {
        if (payload.size() < 2) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        std::uint16_t key_len;
        std::memcpy(&key_len, payload.data(), 2); key_len = ntohs(key_len); payload.remove_prefix(2);
        if (payload.size() < key_len + 2) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        const auto key = payload.substr(0, key_len); payload.remove_prefix(key_len);

        std::uint16_t olen;
        std::memcpy(&olen, payload.data(), 2); olen = ntohs(olen); payload.remove_prefix(2);
        if (payload.size() < olen) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        const auto owner_token = payload.substr(0, olen);
        payload.remove_prefix(olen);

        std::string_view err_msg;
        if (payload.size() >= 2)
        {
            std::uint16_t mlen;
            std::memcpy(&mlen, payload.data(), 2);
            mlen = ntohs(mlen);
            if (payload.size() >= 2 + mlen)
            {
                err_msg = payload.substr(2, mlen);
            }
        }

        const auto res = store_->IdemFail(key, owner_token, err_msg);
        if (!res.ok)
        {
            return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        }

        if (aof_writer_)
        {
            aof_writer_->Append("GK.IDEM_FAIL", "{\"key\":\"" + std::string(key) + "\",\"owner_token\":\"" + std::string(owner_token) + "\"}");
        }

        std::string resp;
        resp.push_back(static_cast<char>(protocol::gkwp2::BinaryStatus::Ok));
        resp.push_back(1);
        return resp;
    }
        case protocol::gkwp2::BinaryOpcode::LockAcquire:
    {
        if (payload.size() < 2) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        std::uint16_t rlen;
        std::memcpy(&rlen, payload.data(), 2); rlen = ntohs(rlen); payload.remove_prefix(2);
        if (payload.size() < rlen + 8 + 2) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        const auto resource = payload.substr(0, rlen); payload.remove_prefix(rlen);

        std::uint64_t ttl_ms;
        std::memcpy(&ttl_ms, payload.data(), 8); ttl_ms = be64toh(ttl_ms); payload.remove_prefix(8);

        std::uint16_t olen;
        std::memcpy(&olen, payload.data(), 2); olen = ntohs(olen); payload.remove_prefix(2);
        std::string_view owner_token;
        if (olen > 0 && payload.size() >= olen)
        {
            owner_token = payload.substr(0, olen);
            payload.remove_prefix(olen);
        }

        bool ephemeral = false;
        if (!payload.empty())
        {
            ephemeral = (payload[0] != 0);
            payload.remove_prefix(1);
        }

        std::uint64_t session_id = 0;
        if (payload.size() >= 8)
        {
            std::memcpy(&session_id, payload.data(), 8);
            session_id = be64toh(session_id);
            payload.remove_prefix(8);
        }

        const auto res = store_->LockAcquire(resource, ttl_ms, owner_token, session_id, ephemeral);
        if (!res.ok)
        {
            return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        }

        if (aof_writer_ && res.acquired)
        {
            aof_writer_->Append("GK.LOCK_ACQUIRE", "{\"resource\":" + protocol::QuoteJson(resource) +
                ",\"ttl_ms\":" + std::to_string(ttl_ms) + ",\"owner_token\":" + protocol::QuoteJson(res.owner_token) + "}");
        }

        std::string resp;
        resp.push_back(static_cast<char>(res.acquired ? protocol::gkwp2::BinaryStatus::Ok : protocol::gkwp2::BinaryStatus::Conflict));
        resp.push_back(res.acquired ? 1 : 0);

        std::uint64_t fencing_net = htobe64(res.fencing_token);
        resp.append(reinterpret_cast<const char*>(&fencing_net), 8);

        std::uint64_t ttl_net = htobe64(res.ttl_remaining_ms);
        resp.append(reinterpret_cast<const char*>(&ttl_net), 8);

        std::uint16_t tok_len = htons(static_cast<std::uint16_t>(res.owner_token.size()));
        resp.append(reinterpret_cast<const char*>(&tok_len), 2);
        resp.append(res.owner_token);
        return resp;
    }
    case protocol::gkwp2::BinaryOpcode::LockRelease:
    {
        if (payload.size() < 2) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        std::uint16_t rlen;
        std::memcpy(&rlen, payload.data(), 2); rlen = ntohs(rlen); payload.remove_prefix(2);
        if (payload.size() < rlen + 2) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        const auto resource = payload.substr(0, rlen); payload.remove_prefix(rlen);

        std::uint16_t olen;
        std::memcpy(&olen, payload.data(), 2); olen = ntohs(olen); payload.remove_prefix(2);
        if (payload.size() < olen) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        const auto owner_token = payload.substr(0, olen);

        const auto res = store_->LockRelease(resource, owner_token);
        if (!res.ok)
        {
            return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        }

        if (aof_writer_)
        {
            aof_writer_->Append("GK.LOCK_RELEASE", "{\"resource\":" + protocol::QuoteJson(resource) +
                ",\"owner_token\":" + protocol::QuoteJson(owner_token) + "}");
        }

        domain::lock::GetGlobalLockWaitQueue().WakeNext(*store_, resource);

        std::string resp;
        resp.push_back(static_cast<char>(protocol::gkwp2::BinaryStatus::Ok));
        resp.push_back(1);
        return resp;
    }
    case protocol::gkwp2::BinaryOpcode::LockExtend:
    {
        if (payload.size() < 2) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        std::uint16_t rlen;
        std::memcpy(&rlen, payload.data(), 2); rlen = ntohs(rlen); payload.remove_prefix(2);
        if (payload.size() < rlen + 2) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        const auto resource = payload.substr(0, rlen); payload.remove_prefix(rlen);

        std::uint16_t olen;
        std::memcpy(&olen, payload.data(), 2); olen = ntohs(olen); payload.remove_prefix(2);
        if (payload.size() < olen + 8) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        const auto owner_token = payload.substr(0, olen); payload.remove_prefix(olen);

        std::uint64_t ttl_ms;
        std::memcpy(&ttl_ms, payload.data(), 8); ttl_ms = be64toh(ttl_ms);

        const auto res = store_->LockExtend(resource, owner_token, ttl_ms);
        if (!res.ok)
        {
            return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        }

        if (aof_writer_)
        {
            aof_writer_->Append("GK.LOCK_EXTEND", "{\"resource\":" + protocol::QuoteJson(resource) +
                ",\"owner_token\":" + protocol::QuoteJson(owner_token) + ",\"ttl_ms\":" + std::to_string(ttl_ms) + "}");
        }

        std::string resp;
        resp.push_back(static_cast<char>(protocol::gkwp2::BinaryStatus::Ok));
        resp.push_back(1);
        std::uint64_t fencing_net = htobe64(res.fencing_token);
        resp.append(reinterpret_cast<const char*>(&fencing_net), 8);
        std::uint64_t ttl_net = htobe64(res.ttl_remaining_ms);
        resp.append(reinterpret_cast<const char*>(&ttl_net), 8);
        return resp;
    }
    case protocol::gkwp2::BinaryOpcode::LockWait:
    {
        if (payload.size() < 2) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        std::uint16_t rlen;
        std::memcpy(&rlen, payload.data(), 2); rlen = ntohs(rlen); payload.remove_prefix(2);
        if (payload.size() < rlen + 8 + 8 + 2) return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        const auto resource = payload.substr(0, rlen); payload.remove_prefix(rlen);

        std::uint64_t ttl_ms;
        std::memcpy(&ttl_ms, payload.data(), 8); ttl_ms = be64toh(ttl_ms); payload.remove_prefix(8);

        std::uint64_t max_wait_ms;
        std::memcpy(&max_wait_ms, payload.data(), 8); max_wait_ms = be64toh(max_wait_ms); payload.remove_prefix(8);

        std::uint16_t olen;
        std::memcpy(&olen, payload.data(), 2); olen = ntohs(olen); payload.remove_prefix(2);
        std::string_view owner_token;
        if (olen > 0 && payload.size() >= olen)
        {
            owner_token = payload.substr(0, olen);
            payload.remove_prefix(olen);
        }

        bool ephemeral = false;
        if (!payload.empty())
        {
            ephemeral = (payload[0] != 0);
            payload.remove_prefix(1);
        }

        std::uint64_t session_id = 0;
        if (payload.size() >= 8)
        {
            std::memcpy(&session_id, payload.data(), 8);
            session_id = be64toh(session_id);
            payload.remove_prefix(8);
        }

        const auto res = domain::lock::GetGlobalLockWaitQueue().WaitOrAcquire(
            *store_, resource, ttl_ms, max_wait_ms, owner_token, session_id, ephemeral);

        if (!res.ok)
        {
            return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
        }

        if (aof_writer_ && res.acquired)
        {
            aof_writer_->Append("GK.LOCK_ACQUIRE", "{\"resource\":" + protocol::QuoteJson(resource) +
                ",\"ttl_ms\":" + std::to_string(ttl_ms) + ",\"owner_token\":" + protocol::QuoteJson(res.owner_token) + "}");
        }

        std::string resp;
        resp.push_back(static_cast<char>(res.acquired ? protocol::gkwp2::BinaryStatus::Ok : protocol::gkwp2::BinaryStatus::Conflict));
        resp.push_back(res.acquired ? 1 : 0);

        std::uint64_t fencing_net = htobe64(res.fencing_token);
        resp.append(reinterpret_cast<const char*>(&fencing_net), 8);

        std::uint64_t ttl_net = htobe64(res.ttl_remaining_ms);
        resp.append(reinterpret_cast<const char*>(&ttl_net), 8);

        std::uint16_t tok_len = htons(static_cast<std::uint16_t>(res.owner_token.size()));
        resp.append(reinterpret_cast<const char*>(&tok_len), 2);
        resp.append(res.owner_token);
        return resp;
    }
default:
        return std::string(1, static_cast<char>(protocol::gkwp2::BinaryStatus::Error));
    }
}

}
