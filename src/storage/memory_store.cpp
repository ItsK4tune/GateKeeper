#include "gatekeeper/storage/memory_store.h"

#include <chrono>
#include <utility>

namespace gatekeeper::storage
{

namespace
{

std::uint64_t CurrentTimeMs()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
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

bool MemoryStore::Set(std::string key, std::string value, WriteCondition condition, std::uint64_t ttl_ms)
{
    const std::lock_guard lock(mutex_);
    const auto now = CurrentTimeMs();
    auto* existing = entries_.Find(key);
    if (existing != nullptr && existing->meta.IsExpired(now))
    {
        entries_.Erase(key);
        existing = nullptr;
    }
    if (condition == WriteCondition::IfAbsent && existing != nullptr)
    {
        return false;
    }
    if (condition == WriteCondition::IfPresent && existing == nullptr)
    {
        return false;
    }
    const auto expire_at = ttl_ms > 0 ? now + ttl_ms : 0;
    if (existing == nullptr)
    {
        Entry entry{key, std::move(value), EntryMetadata{DataType::String, now, expire_at}};
        entries_.Insert(std::move(key), std::move(entry));
    }
    else
    {
        existing->value = std::move(value);
        existing->meta.type = DataType::String;
        existing->meta.expire_at_ms = expire_at;
    }
    return true;
}

std::optional<std::string> MemoryStore::Get(std::string_view key) const
{
    const std::lock_guard lock(mutex_);
    const auto now = CurrentTimeMs();
    const auto key_str = std::string(key);
    auto* existing = entries_.Find(key_str);
    if (existing == nullptr)
    {
        return std::nullopt;
    }
    if (existing->meta.IsExpired(now))
    {
        entries_.Erase(key_str);
        return std::nullopt;
    }
    return existing->value;
}

bool MemoryStore::Del(std::string_view key)
{
    const std::lock_guard lock(mutex_);
    return entries_.Erase(std::string(key));
}

bool MemoryStore::Exists(std::string_view key) const
{
    const std::lock_guard lock(mutex_);
    const auto now = CurrentTimeMs();
    const auto key_str = std::string(key);
    auto* existing = entries_.Find(key_str);
    if (existing == nullptr)
    {
        return false;
    }
    if (existing->meta.IsExpired(now))
    {
        entries_.Erase(key_str);
        return false;
    }
    return true;
}

DataType MemoryStore::Type(std::string_view key) const
{
    const std::lock_guard lock(mutex_);
    const auto now = CurrentTimeMs();
    const auto key_str = std::string(key);
    auto* existing = entries_.Find(key_str);
    if (existing == nullptr)
    {
        return DataType::None;
    }
    if (existing->meta.IsExpired(now))
    {
        entries_.Erase(key_str);
        return DataType::None;
    }
    return existing->meta.type;
}

std::size_t MemoryStore::DbSize() const
{
    const std::lock_guard lock(mutex_);
    const auto now = CurrentTimeMs();
    std::size_t count = 0;
    entries_.ForEach([&](const std::string&, const Entry& entry) {
        if (!entry.meta.IsExpired(now))
        {
            ++count;
        }
    });
    return count;
}

std::vector<std::string> MemoryStore::Keys(std::string_view pattern) const
{
    const std::lock_guard lock(mutex_);
    const auto now = CurrentTimeMs();
    std::vector<std::string> matched;
    entries_.ForEach([&](const std::string& key, const Entry& entry) {
        if (!entry.meta.IsExpired(now) && MatchPattern(pattern, key))
        {
            matched.push_back(key);
        }
    });
    return matched;
}

std::pair<std::size_t, std::vector<std::string>> MemoryStore::Scan(std::size_t cursor, std::size_t count) const
{
    const std::lock_guard lock(mutex_);
    const auto now = CurrentTimeMs();
    auto [next_cursor, keys] = entries_.Scan(cursor, count);
    std::vector<std::string> active_keys;
    for (const auto& k : keys)
    {
        auto* e = entries_.Find(k);
        if (e != nullptr && !e->meta.IsExpired(now))
        {
            active_keys.push_back(k);
        }
    }
    return {next_cursor, std::move(active_keys)};
}

bool MemoryStore::Expire(std::string_view key, std::uint64_t ttl_ms)
{
    const std::lock_guard lock(mutex_);
    const auto now = CurrentTimeMs();
    const auto key_str = std::string(key);
    auto* existing = entries_.Find(key_str);
    if (existing == nullptr || existing->meta.IsExpired(now))
    {
        if (existing != nullptr)
        {
            entries_.Erase(key_str);
        }
        return false;
    }
    existing->meta.expire_at_ms = now + ttl_ms;
    return true;
}

std::int64_t MemoryStore::Ttl(std::string_view key) const
{
    const std::lock_guard lock(mutex_);
    const auto now = CurrentTimeMs();
    const auto key_str = std::string(key);
    auto* existing = entries_.Find(key_str);
    if (existing == nullptr)
    {
        return -2;
    }
    if (existing->meta.IsExpired(now))
    {
        entries_.Erase(key_str);
        return -2;
    }
    if (existing->meta.expire_at_ms == 0)
    {
        return -1;
    }
    return static_cast<std::int64_t>((existing->meta.expire_at_ms - now + 999) / 1000);
}

std::int64_t MemoryStore::Pttl(std::string_view key) const
{
    const std::lock_guard lock(mutex_);
    const auto now = CurrentTimeMs();
    const auto key_str = std::string(key);
    auto* existing = entries_.Find(key_str);
    if (existing == nullptr)
    {
        return -2;
    }
    if (existing->meta.IsExpired(now))
    {
        entries_.Erase(key_str);
        return -2;
    }
    if (existing->meta.expire_at_ms == 0)
    {
        return -1;
    }
    return static_cast<std::int64_t>(existing->meta.expire_at_ms - now);
}

bool MemoryStore::Persist(std::string_view key)
{
    const std::lock_guard lock(mutex_);
    const auto now = CurrentTimeMs();
    const auto key_str = std::string(key);
    auto* existing = entries_.Find(key_str);
    if (existing == nullptr || existing->meta.IsExpired(now))
    {
        if (existing != nullptr)
        {
            entries_.Erase(key_str);
        }
        return false;
    }
    if (existing->meta.expire_at_ms == 0)
    {
        return false;
    }
    existing->meta.expire_at_ms = 0;
    return true;
}

std::size_t MemoryStore::PurgeExpired(std::size_t sample_limit)
{
    const std::lock_guard lock(mutex_);
    const auto now = CurrentTimeMs();
    std::vector<std::string> expired_keys;
    entries_.ForEach([&](const std::string& k, const Entry& e) {
        if (expired_keys.size() < sample_limit && e.meta.IsExpired(now))
        {
            expired_keys.push_back(k);
        }
    });
    for (const auto& k : expired_keys)
    {
        entries_.Erase(k);
    }
    return expired_keys.size();
}

}
