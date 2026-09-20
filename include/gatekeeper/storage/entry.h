#pragma once

#include <cstdint>
#include <string>

namespace gatekeeper::storage
{

enum class DataType : std::uint8_t
{
    None = 0,
    String = 1,
};

struct EntryMetadata
{
    DataType type{DataType::None};
    std::uint64_t created_at_ms{0};
    std::uint64_t expire_at_ms{0};

    [[nodiscard]] bool IsExpired(std::uint64_t now_ms) const noexcept
    {
        return expire_at_ms > 0 && now_ms >= expire_at_ms;
    }
};

struct Entry
{
    std::string key;
    std::string value;
    EntryMetadata meta;
};

struct IncrResult
{
    bool ok{false};
    std::int64_t value{0};
    std::string error_code;
    std::string error_message;
};

struct RateLimitResult
{
    bool ok{true};
    bool allowed{false};
    std::uint64_t remaining{0};
    std::uint64_t retry_after_ms{0};
    std::string error_code;
    std::string error_message;
};

struct RateLimitRecord
{
    std::string key;
    std::uint64_t current_window_idx{0};
    std::uint64_t current_count{0};
    std::uint64_t previous_count{0};
    std::uint64_t window_ms{0};
    std::uint64_t expire_at_ms{0};

    [[nodiscard]] bool IsExpired(std::uint64_t now_ms) const noexcept
    {
        return expire_at_ms > 0 && now_ms >= expire_at_ms;
    }
};

struct Reservation
{
    std::string id;
    std::string key;
    std::uint64_t amount{0};
    std::uint64_t created_at_ms{0};
    std::uint64_t expire_at_ms{0};

    [[nodiscard]] bool IsExpired(std::uint64_t now_ms) const noexcept
    {
        return expire_at_ms > 0 && now_ms >= expire_at_ms;
    }
};

struct ReservationResult
{
    bool ok{true};
    bool reserved{false};
    std::string reservation_id;
    std::uint64_t remaining{0};
    std::string error_code;
    std::string error_message;
};

struct CommitResult
{
    bool ok{true};
    bool committed{false};
    std::uint64_t actual_amount{0};
    std::uint64_t refunded{0};
    std::uint64_t remaining{0};
    std::string error_code;
    std::string error_message;
};

struct RollbackResult
{
    bool ok{true};
    bool rolled_back{false};
    std::uint64_t refunded{0};
    std::uint64_t remaining{0};
    std::string error_code;
    std::string error_message;
};

enum class IdempotencyStatus : std::uint8_t
{
    None = 0,
    InProgress = 1,
    Completed = 2,
    Failed = 3
};

struct IdempotencyRecord
{
    std::string key;
    std::string request_hash;
    IdempotencyStatus status{IdempotencyStatus::None};
    int response_code{0};
    std::string response_body;
    std::string owner_token;
    std::uint64_t created_at_ms{0};
    std::uint64_t expire_at_ms{0};

    [[nodiscard]] bool IsExpired(std::uint64_t now_ms) const noexcept
    {
        return expire_at_ms > 0 && now_ms >= expire_at_ms;
    }
};

enum class IdempotencyAction : std::uint8_t
{
    Execute = 0,
    Park = 1,
    Replay = 2,
    Conflict = 3
};

struct IdempotencyBeginResult
{
    bool ok{true};
    IdempotencyAction action{IdempotencyAction::Execute};
    std::string owner_token;
    int cached_code{0};
    std::string cached_response;
    std::string error_code;
    std::string error_message;
};

struct IdempotencyCompleteResult
{
    bool ok{true};
    bool completed{false};
    std::string error_code;
    std::string error_message;
};

struct IdempotencyFailResult
{
    bool ok{true};
    bool failed{false};
    std::string error_code;
    std::string error_message;
};


struct LockRecord
{
    std::string resource;
    std::string owner_token;
    std::uint64_t fencing_token{0};
    std::uint64_t created_at_ms{0};
    std::uint64_t expire_at_ms{0};
    std::uint64_t session_id{0};
    bool is_ephemeral{false};

    [[nodiscard]] bool IsExpired(std::uint64_t now_ms) const noexcept
    {
        return expire_at_ms > 0 && now_ms >= expire_at_ms;
    }
};

struct LockAcquireResult
{
    bool ok{true};
    bool acquired{false};
    std::string resource;
    std::string owner_token;
    std::uint64_t fencing_token{0};
    std::uint64_t ttl_remaining_ms{0};
    std::string error_code;
    std::string error_message;
};

struct LockReleaseResult
{
    bool ok{true};
    bool released{false};
    std::string error_code;
    std::string error_message;
};

struct LockExtendResult
{
    bool ok{true};
    bool extended{false};
    std::uint64_t ttl_remaining_ms{0};
    std::string error_code;
    std::string error_message;
};

}
