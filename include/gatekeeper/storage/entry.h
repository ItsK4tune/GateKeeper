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

}
