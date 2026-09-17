#include "gatekeeper/storage/memory_store.h"

#include <chrono>
#include <mutex>
#include <utility>

namespace gatekeeper::storage
{

namespace
{
std::uint64_t CurrentTimeMs()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}
}

bool MemoryStore::Set(std::string key, std::string value, WriteCondition condition)
{
    const std::lock_guard lock(mutex_);
    auto* existing = entries_.Find(key);
    if (condition == WriteCondition::IfAbsent && existing != nullptr)
    {
        return false;
    }
    if (condition == WriteCondition::IfPresent && existing == nullptr)
    {
        return false;
    }
    if (existing == nullptr)
    {
        Entry entry{key, std::move(value), EntryMetadata{DataType::String, CurrentTimeMs(), 0}};
        entries_.Insert(std::move(key), std::move(entry));
    }
    else
    {
        existing->value = std::move(value);
        existing->meta.type = DataType::String;
    }
    return true;
}

std::optional<std::string> MemoryStore::Get(std::string_view key) const
{
    const std::lock_guard lock(mutex_);
    const auto* existing = entries_.Find(std::string(key));
    if (existing == nullptr)
    {
        return std::nullopt;
    }
    return existing->value;
}

}
