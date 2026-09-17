#include "gatekeeper/storage/memory_store.h"

#include <chrono>
#include <mutex>
#include <shared_mutex>
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

bool MatchPattern(std::string_view pattern, std::string_view str)
{
    if (pattern == "*")
    {
        return true;
    }
    std::size_t p = 0;
    std::size_t s = 0;
    std::size_t star_p = std::string_view::npos;
    std::size_t match_s = 0;

    while (s < str.size())
    {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == str[s]))
        {
            ++p;
            ++s;
        }
        else if (p < pattern.size() && pattern[p] == '*')
        {
            star_p = p++;
            match_s = s;
        }
        else if (star_p != std::string_view::npos)
        {
            p = star_p + 1;
            s = ++match_s;
        }
        else
        {
            return false;
        }
    }

    while (p < pattern.size() && pattern[p] == '*')
    {
        ++p;
    }

    return p == pattern.size();
}

}

bool MemoryStore::Set(std::string key, std::string value, WriteCondition condition)
{
    const std::unique_lock lock(mutex_);
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
    const std::shared_lock lock(mutex_);
    const auto* existing = entries_.Find(std::string(key));
    if (existing == nullptr)
    {
        return std::nullopt;
    }
    return existing->value;
}

bool MemoryStore::Del(std::string_view key)
{
    const std::unique_lock lock(mutex_);
    return entries_.Erase(std::string(key));
}

bool MemoryStore::Exists(std::string_view key) const
{
    const std::shared_lock lock(mutex_);
    return entries_.Find(std::string(key)) != nullptr;
}

DataType MemoryStore::Type(std::string_view key) const
{
    const std::shared_lock lock(mutex_);
    const auto* existing = entries_.Find(std::string(key));
    if (existing == nullptr)
    {
        return DataType::None;
    }
    return existing->meta.type;
}

std::size_t MemoryStore::DbSize() const
{
    const std::shared_lock lock(mutex_);
    return entries_.Size();
}

std::vector<std::string> MemoryStore::Keys(std::string_view pattern) const
{
    const std::shared_lock lock(mutex_);
    std::vector<std::string> matched;
    entries_.ForEach([&](const std::string& key, const Entry&) {
        if (MatchPattern(pattern, key))
        {
            matched.push_back(key);
        }
    });
    return matched;
}

std::pair<std::size_t, std::vector<std::string>> MemoryStore::Scan(std::size_t cursor, std::size_t count) const
{
    const std::shared_lock lock(mutex_);
    return entries_.Scan(cursor, count);
}

}
