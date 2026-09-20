#pragma once

#include "gatekeeper/storage/entry.h"
#include "gatekeeper/storage/hash_table.h"
#include "gatekeeper/storage/store.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gatekeeper::storage
{

struct StorageShard
{
    mutable std::mutex mutex;
    HashTable<std::string, Entry> entries;
    HashTable<std::string, RateLimitRecord> rate_limits;
    HashTable<std::string, Reservation> reservations;
    HashTable<std::string, IdempotencyRecord> idempotency_records;
};

class MemoryStore final : public Store
{
public:
    static constexpr std::size_t kNumShards = 64;

    bool Set(std::string key, std::string value, WriteCondition condition, std::uint64_t ttl_ms = 0) override;
    std::optional<std::string> Get(std::string_view key) const override;

    bool Del(std::string_view key) override;
    bool Exists(std::string_view key) const override;
    DataType Type(std::string_view key) const override;
    std::size_t DbSize() const override;
    std::vector<std::string> Keys(std::string_view pattern) const override;
    std::pair<std::size_t, std::vector<std::string>> Scan(std::size_t cursor, std::size_t count) const override;

    bool Expire(std::string_view key, std::uint64_t ttl_ms) override;
    std::int64_t Ttl(std::string_view key) const override;
    std::int64_t Pttl(std::string_view key) const override;
    bool Persist(std::string_view key) override;
    std::size_t PurgeExpired(std::size_t sample_limit) override;

    IncrResult IncrBy(std::string_view key, std::int64_t delta, std::uint64_t init_ttl_ms = 0) override;
    RateLimitResult RateLimit(std::string_view key, std::uint64_t limit, std::uint64_t window_ms, std::uint64_t cost = 1) override;

    ReservationResult ReserveQuota(std::string_view key, std::uint64_t amount, std::uint64_t ttl_ms) override;
    CommitResult CommitQuota(std::string_view key, std::string_view reservation_id, std::uint64_t actual_amount) override;
    RollbackResult RollbackQuota(std::string_view key, std::string_view reservation_id) override;

    IdempotencyBeginResult IdemBegin(std::string_view key, std::string_view request_hash, std::uint64_t ttl_ms, std::string_view owner_token) override;
    IdempotencyCompleteResult IdemComplete(std::string_view key, std::string_view owner_token, int response_code, std::string_view response_body) override;
    IdempotencyFailResult IdemFail(std::string_view key, std::string_view owner_token, std::string_view error_message) override;
    std::optional<IdempotencyRecord> IdemGet(std::string_view key) const override;

private:
    StorageShard& GetShard(std::string_view key) const noexcept;

    mutable std::array<StorageShard, kNumShards> shards_;
    mutable std::atomic<std::uint64_t> next_reservation_seq_{1};
    mutable std::atomic<std::uint64_t> next_idempotency_seq_{1};
};

}
