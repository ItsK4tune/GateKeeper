#pragma once

#include "gatekeeper/storage/entry.h"
#include "gatekeeper/storage/hash_table.h"
#include "gatekeeper/storage/store.h"

#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace gatekeeper::storage
{

class MemoryStore final : public Store
{
public:
    bool Set(std::string key, std::string value, WriteCondition condition) override;
    std::optional<std::string> Get(std::string_view key) const override;

private:
    mutable std::mutex mutex_;
    HashTable<std::string, Entry> entries_;
};

using InMemoryStringStore = MemoryStore;

}
