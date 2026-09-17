#include "gatekeeper/storage/memory_store.h"

#include <utility>

namespace gatekeeper::storage
{

bool MemoryStore::Set(std::string key, std::string value, WriteCondition condition)
{
    const std::lock_guard lock(mutex_);
    const auto existing = values_.find(key);
    if (condition == WriteCondition::IfAbsent && existing != values_.end())
    {
        return false;
    }
    if (condition == WriteCondition::IfPresent && existing == values_.end())
    {
        return false;
    }
    if (existing == values_.end())
    {
        values_.emplace(std::move(key), std::move(value));
    }
    else
    {
        existing->second = std::move(value);
    }
    return true;
}

std::optional<std::string> MemoryStore::Get(std::string_view key) const
{
    const std::lock_guard lock(mutex_);
    const auto existing = values_.find(key);
    if (existing == values_.end())
    {
        return std::nullopt;
    }
    return existing->second;
}

}
