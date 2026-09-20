#pragma once

#include "gatekeeper/storage/entry.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gatekeeper::storage
{

enum class WriteCondition
{
    Always,
    IfAbsent,
    IfPresent,
};

class Store
{
public:
    virtual ~Store() = default;

    virtual bool Set(std::string key, std::string value, WriteCondition condition, std::uint64_t ttl_ms = 0) = 0;
    virtual std::optional<std::string> Get(std::string_view key) const = 0;

    virtual bool Del(std::string_view key) = 0;
    virtual bool Exists(std::string_view key) const = 0;
    virtual DataType Type(std::string_view key) const = 0;
    virtual std::size_t DbSize() const = 0;
    virtual std::vector<std::string> Keys(std::string_view pattern) const = 0;
    virtual std::pair<std::size_t, std::vector<std::string>> Scan(std::size_t cursor, std::size_t count) const = 0;

    virtual bool Expire(std::string_view key, std::uint64_t ttl_ms) = 0;
    virtual std::int64_t Ttl(std::string_view key) const = 0;
    virtual std::int64_t Pttl(std::string_view key) const = 0;
    virtual bool Persist(std::string_view key) = 0;
    virtual std::size_t PurgeExpired(std::size_t sample_limit) = 0;

    virtual IncrResult IncrBy(std::string_view key, std::int64_t delta, std::uint64_t init_ttl_ms = 0) = 0;
    virtual RateLimitResult RateLimit(std::string_view key, std::uint64_t limit, std::uint64_t window_ms, std::uint64_t cost = 1) = 0;

    virtual ReservationResult ReserveQuota(std::string_view key, std::uint64_t amount, std::uint64_t ttl_ms) = 0;
    virtual CommitResult CommitQuota(std::string_view key, std::string_view reservation_id, std::uint64_t actual_amount) = 0;
    virtual RollbackResult RollbackQuota(std::string_view key, std::string_view reservation_id) = 0;

    virtual IdempotencyBeginResult IdemBegin(std::string_view key, std::string_view request_hash, std::uint64_t ttl_ms, std::string_view owner_token) = 0;
    virtual IdempotencyCompleteResult IdemComplete(std::string_view key, std::string_view owner_token, int response_code, std::string_view response_body) = 0;
    virtual IdempotencyFailResult IdemFail(std::string_view key, std::string_view owner_token, std::string_view error_message) = 0;
    virtual std::optional<IdempotencyRecord> IdemGet(std::string_view key) const = 0;

    virtual LockAcquireResult LockAcquire(std::string_view resource, std::uint64_t ttl_ms, std::string_view owner_token = "", std::uint64_t session_id = 0, bool is_ephemeral = false) = 0;
    virtual LockReleaseResult LockRelease(std::string_view resource, std::string_view owner_token) = 0;
    virtual LockExtendResult LockExtend(std::string_view resource, std::string_view owner_token, std::uint64_t ttl_ms) = 0;
    virtual std::optional<LockRecord> LockGet(std::string_view resource) const = 0;
    virtual std::vector<std::string> ReleaseSessionLocks(std::uint64_t session_id) = 0;
};

}
