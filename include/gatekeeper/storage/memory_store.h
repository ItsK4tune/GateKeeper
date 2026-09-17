#pragma once

#include "gatekeeper/storage/entry.h"
#include "gatekeeper/storage/hash_table.h"
#include "gatekeeper/storage/store.h"

#include <cstddef>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gatekeeper::storage
{

class MemoryStore final : public Store
{
public:
    bool Set(std::string key, std::string value, WriteCondition condition) override;
    std::optional<std::string> Get(std::string_view key) const override;

    bool Del(std::string_view key) override;
    bool Exists(std::string_view key) const override;
    DataType Type(std::string_view key) const override;
    std::size_t DbSize() const override;
    std::vector<std::string> Keys(std::string_view pattern) const override;
    std::pair<std::size_t, std::vector<std::string>> Scan(std::size_t cursor, std::size_t count) const override;

private:
    mutable std::shared_mutex mutex_;
    HashTable<std::string, Entry> entries_;
};

using InMemoryStringStore = MemoryStore;

}
