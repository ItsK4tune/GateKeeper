#pragma once

#include "gatekeeper/storage/entry.h"
#include "gatekeeper/storage/hash_table.h"
#include "gatekeeper/storage/store.h"

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

class MemoryStore final : public Store
{
public:
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

private:
    mutable std::mutex mutex_;
    mutable HashTable<std::string, Entry> entries_;
};

using InMemoryStringStore = MemoryStore;

}
