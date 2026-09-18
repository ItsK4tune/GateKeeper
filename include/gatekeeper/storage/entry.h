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

}
