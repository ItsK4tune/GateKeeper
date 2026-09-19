#include "gatekeeper/storage/memory_store.h"

#include <chrono>
#include <limits>
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
    std::vector<std::string> current_keys;
    entries_.ForEach([&](const std::string& key, const Entry& entry) {
        if (!entry.meta.IsExpired(now))
        {
            current_keys.push_back(key);
        }
    });

    if (cursor >= current_keys.size())
    {
        return {0, {}};
    }

    std::vector<std::string> batch;
    const std::size_t end = std::min(cursor + count, current_keys.size());
    for (std::size_t i = cursor; i < end; ++i)
    {
        batch.push_back(current_keys[i]);
    }

    std::size_t next_cursor = end >= current_keys.size() ? 0 : end;
    return {next_cursor, std::move(batch)};
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
    if (ttl_ms == 0)
    {
        entries_.Erase(key_str);
        return true;
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
    const auto remaining_ms = existing->meta.expire_at_ms - now;
    return static_cast<std::int64_t>((remaining_ms + 999) / 1000);
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

IncrResult MemoryStore::IncrBy(std::string_view key, std::int64_t delta, std::uint64_t init_ttl_ms)
{
    const std::lock_guard lock(mutex_);
    const auto now = CurrentTimeMs();
    const auto key_str = std::string(key);
    auto* existing = entries_.Find(key_str);
    if (existing != nullptr && existing->meta.IsExpired(now))
    {
        entries_.Erase(key_str);
        existing = nullptr;
    }

    if (existing == nullptr)
    {
        const auto expire_at = init_ttl_ms > 0 ? now + init_ttl_ms : 0;
        Entry entry{key_str, std::to_string(delta), EntryMetadata{DataType::String, now, expire_at}};
        entries_.Insert(key_str, std::move(entry));
        return {true, delta, {}, {}};
    }

    if (existing->meta.type != DataType::String)
    {
        return {false, 0, "WRONGTYPE", "Operation against a key holding the wrong kind of value"};
    }

    std::int64_t current_val = 0;
    try
    {
        std::size_t idx = 0;
        current_val = std::stoll(existing->value, &idx);
        if (idx != existing->value.size())
        {
            return {false, 0, "ERR_NOT_AN_INTEGER", "value is not an integer or out of range"};
        }
    }
    catch (...)
    {
        return {false, 0, "ERR_NOT_AN_INTEGER", "value is not an integer or out of range"};
    }

    if ((delta > 0 && current_val > std::numeric_limits<std::int64_t>::max() - delta) ||
        (delta < 0 && current_val < std::numeric_limits<std::int64_t>::min() - delta))
    {
        return {false, 0, "ERR_OVERFLOW", "increment or decrement would overflow"};
    }

    current_val += delta;
    existing->value = std::to_string(current_val);
    return {true, current_val, {}, {}};
}

RateLimitResult MemoryStore::RateLimit(std::string_view key, std::uint64_t limit, std::uint64_t window_ms, std::uint64_t cost)
{
    if (limit == 0)
    {
        return {false, false, 0, 0, "INVALID_ARGUMENTS", "limit must be greater than zero"};
    }
    if (window_ms == 0)
    {
        return {false, false, 0, 0, "INVALID_ARGUMENTS", "window_ms must be greater than zero"};
    }
    if (cost == 0)
    {
        cost = 1;
    }

    const std::lock_guard lock(mutex_);
    const auto now = CurrentTimeMs();
    const auto key_str = std::string(key);
    auto* existing = entries_.Find(key_str);
    if (existing != nullptr && existing->meta.IsExpired(now))
    {
        entries_.Erase(key_str);
        existing = nullptr;
    }

    if (existing == nullptr)
    {
        if (cost <= limit)
        {
            const auto expire_at = now + window_ms;
            Entry entry{key_str, std::to_string(cost), EntryMetadata{DataType::String, now, expire_at}};
            entries_.Insert(key_str, std::move(entry));
            const auto remaining = limit - cost;
            return {true, true, remaining, 0, {}, {}};
        }
        else
        {
            return {true, false, 0, window_ms, {}, {}};
        }
    }

    std::uint64_t current_count = 0;
    try
    {
        std::size_t idx = 0;
        current_count = std::stoull(existing->value, &idx);
        if (idx != existing->value.size())
        {
            return {false, false, 0, 0, "ERR_NOT_AN_INTEGER", "value is not an integer"};
        }
    }
    catch (...)
    {
        return {false, false, 0, 0, "ERR_NOT_AN_INTEGER", "value is not an integer"};
    }

    std::uint64_t retry_after = 0;
    if (existing->meta.expire_at_ms > now)
    {
        retry_after = existing->meta.expire_at_ms - now;
    }
    else
    {
        existing->meta.expire_at_ms = now + window_ms;
        retry_after = window_ms;
    }

    if (current_count + cost <= limit)
    {
        current_count += cost;
        existing->value = std::to_string(current_count);
        const auto remaining = limit - current_count;
        return {true, true, remaining, 0, {}, {}};
    }

    return {true, false, limit >= current_count ? limit - current_count : 0, retry_after, {}, {}};
}

ReservationResult MemoryStore::ReserveQuota(std::string_view key, std::uint64_t amount, std::uint64_t ttl_ms)
{
    if (amount == 0)
    {
        return {false, false, "", 0, "INVALID_ARGUMENTS", "amount must be greater than zero"};
    }
    if (ttl_ms == 0)
    {
        return {false, false, "", 0, "INVALID_ARGUMENTS", "ttl_ms must be greater than zero"};
    }

    const std::lock_guard lock(mutex_);
    const auto now = CurrentTimeMs();
    const auto key_str = std::string(key);
    auto* existing = entries_.Find(key_str);
    if (existing != nullptr && existing->meta.IsExpired(now))
    {
        entries_.Erase(key_str);
        existing = nullptr;
    }

    if (existing == nullptr)
    {
        return {true, false, "", 0, "INSUFFICIENT_QUOTA", "quota key does not exist"};
    }

    std::uint64_t balance = 0;
    try
    {
        std::size_t idx = 0;
        balance = std::stoull(existing->value, &idx);
        if (idx != existing->value.size())
        {
            return {false, false, "", 0, "ERR_NOT_AN_INTEGER", "quota balance is not an integer"};
        }
    }
    catch (...)
    {
        return {false, false, "", 0, "ERR_NOT_AN_INTEGER", "quota balance is not an integer"};
    }

    if (balance < amount)
    {
        return {true, false, "", balance, "INSUFFICIENT_QUOTA", "insufficient quota balance"};
    }

    balance -= amount;
    existing->value = std::to_string(balance);

    const auto res_id = "res_" + std::to_string(now) + "_" + std::to_string(next_reservation_seq_++);
    Reservation res{res_id, key_str, amount, now, now + ttl_ms};
    reservations_.Insert(res_id, std::move(res));

    return {true, true, res_id, balance, {}, {}};
}

CommitResult MemoryStore::CommitQuota(std::string_view key, std::string_view reservation_id, std::uint64_t actual_amount)
{
    const std::lock_guard lock(mutex_);
    const auto now = CurrentTimeMs();
    const auto res_id_str = std::string(reservation_id);
    auto* res = reservations_.Find(res_id_str);

    if (res == nullptr || res->key != key)
    {
        return {false, false, 0, 0, 0, "RESERVATION_NOT_FOUND", "reservation not found or key mismatch"};
    }

    if (res->IsExpired(now))
    {
        auto* target = entries_.Find(res->key);
        if (target != nullptr)
        {
            try
            {
                auto b = std::stoull(target->value);
                b += res->amount;
                target->value = std::to_string(b);
            }
            catch (...)
            {
                target->value = std::to_string(res->amount);
            }
        }
        reservations_.Erase(res_id_str);
        return {false, false, 0, 0, 0, "RESERVATION_EXPIRED", "reservation expired and was automatically rolled back"};
    }

    const auto reserved_amount = res->amount;
    const auto key_str = res->key;
    reservations_.Erase(res_id_str);

    auto* target = entries_.Find(key_str);
    std::uint64_t balance = 0;
    if (target != nullptr)
    {
        try
        {
            balance = std::stoull(target->value);
        }
        catch (...)
        {
            balance = 0;
        }
    }

    std::uint64_t refunded = 0;
    if (actual_amount <= reserved_amount)
    {
        refunded = reserved_amount - actual_amount;
        balance += refunded;
    }
    else
    {
        const auto extra = actual_amount - reserved_amount;
        if (balance >= extra)
        {
            balance -= extra;
        }
        else
        {
            balance = 0;
        }
    }

    if (target != nullptr)
    {
        target->value = std::to_string(balance);
    }
    else
    {
        Entry entry{key_str, std::to_string(balance), EntryMetadata{DataType::String, now, 0}};
        entries_.Insert(key_str, std::move(entry));
    }

    return {true, true, actual_amount, refunded, balance, {}, {}};
}

RollbackResult MemoryStore::RollbackQuota(std::string_view key, std::string_view reservation_id)
{
    const std::lock_guard lock(mutex_);
    const auto now = CurrentTimeMs();
    const auto res_id_str = std::string(reservation_id);
    auto* res = reservations_.Find(res_id_str);

    if (res == nullptr || res->key != key)
    {
        return {false, false, 0, 0, "RESERVATION_NOT_FOUND", "reservation not found or key mismatch"};
    }

    const auto refund_amount = res->amount;
    const auto key_str = res->key;
    const bool was_expired = res->IsExpired(now);
    reservations_.Erase(res_id_str);

    auto* target = entries_.Find(key_str);
    std::uint64_t balance = 0;
    if (target != nullptr)
    {
        try
        {
            balance = std::stoull(target->value);
        }
        catch (...)
        {
            balance = 0;
        }
    }

    if (!was_expired)
    {
        balance += refund_amount;
        if (target != nullptr)
        {
            target->value = std::to_string(balance);
        }
        else
        {
            Entry entry{key_str, std::to_string(balance), EntryMetadata{DataType::String, now, 0}};
            entries_.Insert(key_str, std::move(entry));
        }
    }

    return {true, true, refund_amount, balance, {}, {}};
}

IdempotencyBeginResult MemoryStore::IdemBegin(
    std::string_view key,
    std::string_view request_hash,
    std::uint64_t ttl_ms,
    std::string_view owner_token)
{
    const std::lock_guard lock(mutex_);
    const auto now = CurrentTimeMs();
    const auto key_str = std::string(key);

    auto* record = idempotency_records_.Find(key_str);
    if (record != nullptr && record->IsExpired(now))
    {
        idempotency_records_.Erase(key_str);
        record = nullptr;
    }

    if (record == nullptr)
    {
        std::string token = owner_token.empty()
            ? ("idem_" + std::to_string(now) + "_" + std::to_string(next_idempotency_seq_++))
            : std::string(owner_token);

        IdempotencyRecord rec;
        rec.key = key_str;
        rec.request_hash = std::string(request_hash);
        rec.status = IdempotencyStatus::InProgress;
        rec.owner_token = token;
        rec.created_at_ms = now;
        rec.expire_at_ms = ttl_ms > 0 ? (now + ttl_ms) : 0;
        rec.response_code = 0;
        rec.response_body = "";

        idempotency_records_.Insert(key_str, std::move(rec));
        return {true, IdempotencyAction::Execute, token, 0, "", {}, {}};
    }

    if (record->request_hash != request_hash)
    {
        return {false, IdempotencyAction::Conflict, "", 0, "", "ERR_IDEMPOTENCY_CONFLICT", "Request hash mismatch for idempotency key"};
    }

    if (record->status == IdempotencyStatus::InProgress)
    {
        if (!owner_token.empty() && record->owner_token == owner_token)
        {
            return {true, IdempotencyAction::Execute, record->owner_token, 0, "", {}, {}};
        }
        return {true, IdempotencyAction::Park, record->owner_token, 0, "", {}, {}};
    }

    if (record->status == IdempotencyStatus::Completed)
    {
        return {true, IdempotencyAction::Replay, record->owner_token, record->response_code, record->response_body, {}, {}};
    }

    if (record->status == IdempotencyStatus::Failed)
    {
        std::string token = owner_token.empty()
            ? ("idem_" + std::to_string(now) + "_" + std::to_string(next_idempotency_seq_++))
            : std::string(owner_token);

        record->status = IdempotencyStatus::InProgress;
        record->owner_token = token;
        record->created_at_ms = now;
        record->expire_at_ms = ttl_ms > 0 ? (now + ttl_ms) : 0;
        record->response_code = 0;
        record->response_body = "";

        return {true, IdempotencyAction::Execute, token, 0, "", {}, {}};
    }

    return {false, IdempotencyAction::Conflict, "", 0, "", "ERR_UNKNOWN_STATUS", "Unknown idempotency status"};
}

IdempotencyCompleteResult MemoryStore::IdemComplete(
    std::string_view key,
    std::string_view owner_token,
    int response_code,
    std::string_view response_body)
{
    const std::lock_guard lock(mutex_);
    const auto now = CurrentTimeMs();
    const auto key_str = std::string(key);

    auto* record = idempotency_records_.Find(key_str);
    if (record == nullptr)
    {
        return {false, false, "ERR_NOT_FOUND", "Idempotency record not found"};
    }

    if (record->IsExpired(now))
    {
        idempotency_records_.Erase(key_str);
        return {false, false, "ERR_EXPIRED", "Idempotency record has expired"};
    }

    if (!owner_token.empty() && record->owner_token != owner_token)
    {
        return {false, false, "ERR_TOKEN_MISMATCH", "Owner token mismatch"};
    }

    if (record->status == IdempotencyStatus::Completed)
    {
        return {true, true, {}, {}};
    }

    record->status = IdempotencyStatus::Completed;
    record->response_code = response_code;
    record->response_body = std::string(response_body);

    return {true, true, {}, {}};
}

IdempotencyFailResult MemoryStore::IdemFail(
    std::string_view key,
    std::string_view owner_token,
    std::string_view error_message)
{
    const std::lock_guard lock(mutex_);
    const auto now = CurrentTimeMs();
    const auto key_str = std::string(key);

    auto* record = idempotency_records_.Find(key_str);
    if (record == nullptr)
    {
        return {false, false, "ERR_NOT_FOUND", "Idempotency record not found"};
    }

    if (record->IsExpired(now))
    {
        idempotency_records_.Erase(key_str);
        return {false, false, "ERR_EXPIRED", "Idempotency record has expired"};
    }

    if (!owner_token.empty() && record->owner_token != owner_token)
    {
        return {false, false, "ERR_TOKEN_MISMATCH", "Owner token mismatch"};
    }

    record->status = IdempotencyStatus::Failed;
    record->response_body = std::string(error_message);

    return {true, true, {}, {}};
}

std::optional<IdempotencyRecord> MemoryStore::IdemGet(std::string_view key) const
{
    const std::lock_guard lock(mutex_);
    const auto now = CurrentTimeMs();
    const auto key_str = std::string(key);

    auto* record = idempotency_records_.Find(key_str);
    if (record == nullptr)
    {
        return std::nullopt;
    }

    if (record->IsExpired(now))
    {
        idempotency_records_.Erase(key_str);
        return std::nullopt;
    }

    return *record;
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

    std::vector<std::string> expired_reservations;
    reservations_.ForEach([&](const std::string& id, const Reservation& res) {
        if (expired_reservations.size() < sample_limit && res.IsExpired(now))
        {
            expired_reservations.push_back(id);
        }
    });
    for (const auto& id : expired_reservations)
    {
        auto* res = reservations_.Find(id);
        if (res != nullptr)
        {
            auto* target = entries_.Find(res->key);
            if (target != nullptr)
            {
                try
                {
                    auto bal = std::stoull(target->value);
                    bal += res->amount;
                    target->value = std::to_string(bal);
                }
                catch (...)
                {
                    target->value = std::to_string(res->amount);
                }
            }
            else
            {
                Entry entry{res->key, std::to_string(res->amount), EntryMetadata{DataType::String, now, 0}};
                entries_.Insert(res->key, std::move(entry));
            }
            reservations_.Erase(id);
        }
    }

    std::vector<std::string> expired_idempotencies;
    idempotency_records_.ForEach([&](const std::string& k, const IdempotencyRecord& rec) {
        if (expired_idempotencies.size() < sample_limit && rec.IsExpired(now))
        {
            expired_idempotencies.push_back(k);
        }
    });
    for (const auto& k : expired_idempotencies)
    {
        idempotency_records_.Erase(k);
    }

    return expired_keys.size() + expired_reservations.size() + expired_idempotencies.size();
}

}
