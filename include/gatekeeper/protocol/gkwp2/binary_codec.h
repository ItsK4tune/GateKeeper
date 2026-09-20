#pragma once

#include <arpa/inet.h>
#include <cstdint>
#include <cstring>
#include <endian.h>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gatekeeper::protocol::gkwp2
{

enum class BinaryOpcode : std::uint16_t
{
    Ping = 0x0001,
    Set = 0x0010,
    Get = 0x0011,
    Del = 0x0012,
    Exists = 0x0013,
    Type = 0x0014,
    Expire = 0x0015,
    Ttl = 0x0016,
    Incr = 0x0020,
    Decr = 0x0021,
    IncrBy = 0x0022,
    RateLimit = 0x0100,
    Reserve = 0x0101,
    Commit = 0x0102,
    Rollback = 0x0103,
    QuotaInit = 0x0110,
    QuotaGet = 0x0111,
    IdemBegin = 0x0120,
    IdemComplete = 0x0121,
    IdemFail = 0x0122,
    LockAcquire = 0x0130,
    LockRelease = 0x0131,
    LockExtend = 0x0132,
    LockWait = 0x0133,
};

enum class BinaryStatus : std::uint8_t
{
    Ok = 0x00,
    Error = 0x01,
    NotFound = 0x02,
    Denied = 0x03,
    Conflict = 0x04,
};

enum class SetCondition : std::uint8_t
{
    Always = 0,
    IfAbsent = 1,
    IfPresent = 2,
};

class BinaryCodec
{
public:
    static std::optional<BinaryOpcode> PeekOpcode(std::span<const std::uint8_t> payload) noexcept
    {
        if (payload.size() < 2) return std::nullopt;
        std::uint16_t net_val;
        std::memcpy(&net_val, payload.data(), 2);
        return static_cast<BinaryOpcode>(ntohs(net_val));
    }

    static std::string EncodePing()
    {
        std::string out;
        out.resize(2);
        std::uint16_t op = htons(static_cast<std::uint16_t>(BinaryOpcode::Ping));
        std::memcpy(out.data(), &op, 2);
        return out;
    }

    static std::string EncodeGet(std::string_view key)
    {
        std::string out;
        out.reserve(2 + 2 + key.size());
        std::uint16_t op = htons(static_cast<std::uint16_t>(BinaryOpcode::Get));
        std::uint16_t klen = htons(static_cast<std::uint16_t>(key.size()));
        out.append(reinterpret_cast<const char*>(&op), 2);
        out.append(reinterpret_cast<const char*>(&klen), 2);
        out.append(key);
        return out;
    }

    static std::string EncodeSet(std::string_view key, std::string_view val, SetCondition cond = SetCondition::Always, std::uint64_t ttl_ms = 0)
    {
        std::string out;
        out.reserve(2 + 2 + key.size() + 4 + val.size() + 8 + 1);
        std::uint16_t op = htons(static_cast<std::uint16_t>(BinaryOpcode::Set));
        std::uint16_t klen = htons(static_cast<std::uint16_t>(key.size()));
        std::uint32_t vlen = htonl(static_cast<std::uint32_t>(val.size()));
        std::uint64_t ttl_net = htobe64(ttl_ms);
        auto cond_byte = static_cast<std::uint8_t>(cond);

        out.append(reinterpret_cast<const char*>(&op), 2);
        out.append(reinterpret_cast<const char*>(&klen), 2);
        out.append(key);
        out.append(reinterpret_cast<const char*>(&vlen), 4);
        out.append(val);
        out.append(reinterpret_cast<const char*>(&ttl_net), 8);
        out.push_back(static_cast<char>(cond_byte));
        return out;
    }

    static std::string EncodeDel(std::string_view key)
    {
        std::string out;
        out.reserve(2 + 2 + key.size());
        std::uint16_t op = htons(static_cast<std::uint16_t>(BinaryOpcode::Del));
        std::uint16_t klen = htons(static_cast<std::uint16_t>(key.size()));
        out.append(reinterpret_cast<const char*>(&op), 2);
        out.append(reinterpret_cast<const char*>(&klen), 2);
        out.append(key);
        return out;
    }

    static std::string EncodeExists(std::string_view key)
    {
        std::string out;
        out.reserve(2 + 2 + key.size());
        std::uint16_t op = htons(static_cast<std::uint16_t>(BinaryOpcode::Exists));
        std::uint16_t klen = htons(static_cast<std::uint16_t>(key.size()));
        out.append(reinterpret_cast<const char*>(&op), 2);
        out.append(reinterpret_cast<const char*>(&klen), 2);
        out.append(key);
        return out;
    }

    static std::string EncodeType(std::string_view key)
    {
        std::string out;
        out.reserve(2 + 2 + key.size());
        std::uint16_t op = htons(static_cast<std::uint16_t>(BinaryOpcode::Type));
        std::uint16_t klen = htons(static_cast<std::uint16_t>(key.size()));
        out.append(reinterpret_cast<const char*>(&op), 2);
        out.append(reinterpret_cast<const char*>(&klen), 2);
        out.append(key);
        return out;
    }

    static std::string EncodeExpire(std::string_view key, std::uint64_t ttl_ms)
    {
        std::string out;
        out.reserve(2 + 2 + key.size() + 8);
        std::uint16_t op = htons(static_cast<std::uint16_t>(BinaryOpcode::Expire));
        std::uint16_t klen = htons(static_cast<std::uint16_t>(key.size()));
        std::uint64_t ttl_net = htobe64(ttl_ms);
        out.append(reinterpret_cast<const char*>(&op), 2);
        out.append(reinterpret_cast<const char*>(&klen), 2);
        out.append(key);
        out.append(reinterpret_cast<const char*>(&ttl_net), 8);
        return out;
    }

    static std::string EncodeTtl(std::string_view key)
    {
        std::string out;
        out.reserve(2 + 2 + key.size());
        std::uint16_t op = htons(static_cast<std::uint16_t>(BinaryOpcode::Ttl));
        std::uint16_t klen = htons(static_cast<std::uint16_t>(key.size()));
        out.append(reinterpret_cast<const char*>(&op), 2);
        out.append(reinterpret_cast<const char*>(&klen), 2);
        out.append(key);
        return out;
    }

    static std::string EncodeIncrBy(std::string_view key, std::int64_t delta, std::uint64_t init_ttl_ms = 0)
    {
        std::string out;
        out.reserve(2 + 2 + key.size() + 8 + 8);
        std::uint16_t op = htons(static_cast<std::uint16_t>(BinaryOpcode::Incr));
        std::uint16_t klen = htons(static_cast<std::uint16_t>(key.size()));
        std::int64_t delta_net = htobe64(static_cast<std::uint64_t>(delta));
        std::uint64_t ttl_net = htobe64(init_ttl_ms);
        out.append(reinterpret_cast<const char*>(&op), 2);
        out.append(reinterpret_cast<const char*>(&klen), 2);
        out.append(key);
        out.append(reinterpret_cast<const char*>(&delta_net), 8);
        out.append(reinterpret_cast<const char*>(&ttl_net), 8);
        return out;
    }

    static std::string EncodeRateLimit(std::string_view key, std::uint64_t limit, std::uint64_t window_ms, std::uint32_t cost = 1)
    {
        std::string out;
        out.reserve(2 + 2 + key.size() + 8 + 8 + 4);
        std::uint16_t op = htons(static_cast<std::uint16_t>(BinaryOpcode::RateLimit));
        std::uint16_t klen = htons(static_cast<std::uint16_t>(key.size()));
        std::uint64_t limit_net = htobe64(limit);
        std::uint64_t win_net = htobe64(window_ms);
        std::uint32_t cost_net = htonl(cost);
        out.append(reinterpret_cast<const char*>(&op), 2);
        out.append(reinterpret_cast<const char*>(&klen), 2);
        out.append(key);
        out.append(reinterpret_cast<const char*>(&limit_net), 8);
        out.append(reinterpret_cast<const char*>(&win_net), 8);
        out.append(reinterpret_cast<const char*>(&cost_net), 4);
        return out;
    }

    static std::string EncodeQuotaInit(std::string_view key, std::uint64_t amount)
    {
        std::string out;
        out.reserve(2 + 2 + key.size() + 8);
        std::uint16_t op = htons(static_cast<std::uint16_t>(BinaryOpcode::QuotaInit));
        std::uint16_t klen = htons(static_cast<std::uint16_t>(key.size()));
        std::uint64_t amt_net = htobe64(amount);
        out.append(reinterpret_cast<const char*>(&op), 2);
        out.append(reinterpret_cast<const char*>(&klen), 2);
        out.append(key);
        out.append(reinterpret_cast<const char*>(&amt_net), 8);
        return out;
    }

    static std::string EncodeQuotaGet(std::string_view key)
    {
        std::string out;
        out.reserve(2 + 2 + key.size());
        std::uint16_t op = htons(static_cast<std::uint16_t>(BinaryOpcode::QuotaGet));
        std::uint16_t klen = htons(static_cast<std::uint16_t>(key.size()));
        out.append(reinterpret_cast<const char*>(&op), 2);
        out.append(reinterpret_cast<const char*>(&klen), 2);
        out.append(key);
        return out;
    }

    static std::string EncodeReserve(std::string_view key, std::uint64_t amount, std::uint64_t ttl_ms)
    {
        std::string out;
        out.reserve(2 + 2 + key.size() + 8 + 8);
        std::uint16_t op = htons(static_cast<std::uint16_t>(BinaryOpcode::Reserve));
        std::uint16_t klen = htons(static_cast<std::uint16_t>(key.size()));
        std::uint64_t amt_net = htobe64(amount);
        std::uint64_t ttl_net = htobe64(ttl_ms);
        out.append(reinterpret_cast<const char*>(&op), 2);
        out.append(reinterpret_cast<const char*>(&klen), 2);
        out.append(key);
        out.append(reinterpret_cast<const char*>(&amt_net), 8);
        out.append(reinterpret_cast<const char*>(&ttl_net), 8);
        return out;
    }

    static std::string EncodeCommit(std::string_view key, std::string_view res_id, std::uint64_t actual_amount)
    {
        std::string out;
        out.reserve(2 + 2 + key.size() + 2 + res_id.size() + 8);
        std::uint16_t op = htons(static_cast<std::uint16_t>(BinaryOpcode::Commit));
        std::uint16_t klen = htons(static_cast<std::uint16_t>(key.size()));
        std::uint16_t rlen = htons(static_cast<std::uint16_t>(res_id.size()));
        std::uint64_t amt_net = htobe64(actual_amount);
        out.append(reinterpret_cast<const char*>(&op), 2);
        out.append(reinterpret_cast<const char*>(&klen), 2);
        out.append(key);
        out.append(reinterpret_cast<const char*>(&rlen), 2);
        out.append(res_id);
        out.append(reinterpret_cast<const char*>(&amt_net), 8);
        return out;
    }

    static std::string EncodeRollback(std::string_view key, std::string_view res_id)
    {
        std::string out;
        out.reserve(2 + 2 + key.size() + 2 + res_id.size());
        std::uint16_t op = htons(static_cast<std::uint16_t>(BinaryOpcode::Rollback));
        std::uint16_t klen = htons(static_cast<std::uint16_t>(key.size()));
        std::uint16_t rlen = htons(static_cast<std::uint16_t>(res_id.size()));
        out.append(reinterpret_cast<const char*>(&op), 2);
        out.append(reinterpret_cast<const char*>(&klen), 2);
        out.append(key);
        out.append(reinterpret_cast<const char*>(&rlen), 2);
        out.append(res_id);
        return out;
    }

    static std::string EncodeIdemBegin(std::string_view key, std::string_view request_hash, std::uint64_t ttl_ms = 0, std::string_view owner_token = "")
    {
        std::string out;
        out.reserve(2 + 2 + key.size() + 2 + request_hash.size() + 8 + 2 + owner_token.size());
        std::uint16_t op = htons(static_cast<std::uint16_t>(BinaryOpcode::IdemBegin));
        std::uint16_t klen = htons(static_cast<std::uint16_t>(key.size()));
        std::uint16_t hlen = htons(static_cast<std::uint16_t>(request_hash.size()));
        std::uint64_t ttl_net = htobe64(ttl_ms);
        std::uint16_t olen = htons(static_cast<std::uint16_t>(owner_token.size()));
        out.append(reinterpret_cast<const char*>(&op), 2);
        out.append(reinterpret_cast<const char*>(&klen), 2);
        out.append(key);
        out.append(reinterpret_cast<const char*>(&hlen), 2);
        out.append(request_hash);
        out.append(reinterpret_cast<const char*>(&ttl_net), 8);
        out.append(reinterpret_cast<const char*>(&olen), 2);
        out.append(owner_token);
        return out;
    }

    static std::string EncodeIdemComplete(std::string_view key, std::string_view owner_token, int response_code, std::string_view response_body)
    {
        std::string out;
        out.reserve(2 + 2 + key.size() + 2 + owner_token.size() + 2 + 4 + response_body.size());
        std::uint16_t op = htons(static_cast<std::uint16_t>(BinaryOpcode::IdemComplete));
        std::uint16_t klen = htons(static_cast<std::uint16_t>(key.size()));
        std::uint16_t olen = htons(static_cast<std::uint16_t>(owner_token.size()));
        std::uint16_t code_net = htons(static_cast<std::uint16_t>(response_code));
        std::uint32_t blen = htonl(static_cast<std::uint32_t>(response_body.size()));
        out.append(reinterpret_cast<const char*>(&op), 2);
        out.append(reinterpret_cast<const char*>(&klen), 2);
        out.append(key);
        out.append(reinterpret_cast<const char*>(&olen), 2);
        out.append(owner_token);
        out.append(reinterpret_cast<const char*>(&code_net), 2);
        out.append(reinterpret_cast<const char*>(&blen), 4);
        out.append(response_body);
        return out;
    }

    static std::string EncodeIdemFail(std::string_view key, std::string_view owner_token, std::string_view error_message = "")
    {
        std::string out;
        out.reserve(2 + 2 + key.size() + 2 + owner_token.size() + 2 + error_message.size());
        std::uint16_t op = htons(static_cast<std::uint16_t>(BinaryOpcode::IdemFail));
        std::uint16_t klen = htons(static_cast<std::uint16_t>(key.size()));
        std::uint16_t olen = htons(static_cast<std::uint16_t>(owner_token.size()));
        std::uint16_t mlen = htons(static_cast<std::uint16_t>(error_message.size()));
        out.append(reinterpret_cast<const char*>(&op), 2);
        out.append(reinterpret_cast<const char*>(&klen), 2);
        out.append(key);
        out.append(reinterpret_cast<const char*>(&olen), 2);
        out.append(owner_token);
        out.append(reinterpret_cast<const char*>(&mlen), 2);
        out.append(error_message);
        return out;
    }

    static std::string EncodeLockAcquire(std::string_view resource, std::uint64_t ttl_ms, std::string_view owner_token = "", bool ephemeral = false, std::uint64_t session_id = 0)
    {
        std::string out;
        out.reserve(2 + 2 + resource.size() + 8 + 2 + owner_token.size() + 1 + 8);
        std::uint16_t op = htons(static_cast<std::uint16_t>(BinaryOpcode::LockAcquire));
        std::uint16_t rlen = htons(static_cast<std::uint16_t>(resource.size()));
        std::uint64_t ttl_net = htobe64(ttl_ms);
        std::uint16_t olen = htons(static_cast<std::uint16_t>(owner_token.size()));
        std::uint8_t eph = ephemeral ? 1 : 0;
        std::uint64_t sess_net = htobe64(session_id);

        out.append(reinterpret_cast<const char*>(&op), 2);
        out.append(reinterpret_cast<const char*>(&rlen), 2);
        out.append(resource);
        out.append(reinterpret_cast<const char*>(&ttl_net), 8);
        out.append(reinterpret_cast<const char*>(&olen), 2);
        out.append(owner_token);
        out.push_back(static_cast<char>(eph));
        out.append(reinterpret_cast<const char*>(&sess_net), 8);
        return out;
    }

    static std::string EncodeLockRelease(std::string_view resource, std::string_view owner_token)
    {
        std::string out;
        out.reserve(2 + 2 + resource.size() + 2 + owner_token.size());
        std::uint16_t op = htons(static_cast<std::uint16_t>(BinaryOpcode::LockRelease));
        std::uint16_t rlen = htons(static_cast<std::uint16_t>(resource.size()));
        std::uint16_t olen = htons(static_cast<std::uint16_t>(owner_token.size()));

        out.append(reinterpret_cast<const char*>(&op), 2);
        out.append(reinterpret_cast<const char*>(&rlen), 2);
        out.append(resource);
        out.append(reinterpret_cast<const char*>(&olen), 2);
        out.append(owner_token);
        return out;
    }

    static std::string EncodeLockExtend(std::string_view resource, std::string_view owner_token, std::uint64_t ttl_ms)
    {
        std::string out;
        out.reserve(2 + 2 + resource.size() + 2 + owner_token.size() + 8);
        std::uint16_t op = htons(static_cast<std::uint16_t>(BinaryOpcode::LockExtend));
        std::uint16_t rlen = htons(static_cast<std::uint16_t>(resource.size()));
        std::uint16_t olen = htons(static_cast<std::uint16_t>(owner_token.size()));
        std::uint64_t ttl_net = htobe64(ttl_ms);

        out.append(reinterpret_cast<const char*>(&op), 2);
        out.append(reinterpret_cast<const char*>(&rlen), 2);
        out.append(resource);
        out.append(reinterpret_cast<const char*>(&olen), 2);
        out.append(owner_token);
        out.append(reinterpret_cast<const char*>(&ttl_net), 8);
        return out;
    }

    static std::string EncodeLockWait(std::string_view resource, std::uint64_t ttl_ms, std::uint64_t max_wait_ms, std::string_view owner_token = "", bool ephemeral = false, std::uint64_t session_id = 0)
    {
        std::string out;
        out.reserve(2 + 2 + resource.size() + 8 + 8 + 2 + owner_token.size() + 1 + 8);
        std::uint16_t op = htons(static_cast<std::uint16_t>(BinaryOpcode::LockWait));
        std::uint16_t rlen = htons(static_cast<std::uint16_t>(resource.size()));
        std::uint64_t ttl_net = htobe64(ttl_ms);
        std::uint64_t wait_net = htobe64(max_wait_ms);
        std::uint16_t olen = htons(static_cast<std::uint16_t>(owner_token.size()));
        std::uint8_t eph = ephemeral ? 1 : 0;
        std::uint64_t sess_net = htobe64(session_id);

        out.append(reinterpret_cast<const char*>(&op), 2);
        out.append(reinterpret_cast<const char*>(&rlen), 2);
        out.append(resource);
        out.append(reinterpret_cast<const char*>(&ttl_net), 8);
        out.append(reinterpret_cast<const char*>(&wait_net), 8);
        out.append(reinterpret_cast<const char*>(&olen), 2);
        out.append(owner_token);
        out.push_back(static_cast<char>(eph));
        out.append(reinterpret_cast<const char*>(&sess_net), 8);
        return out;
    }
};

}
