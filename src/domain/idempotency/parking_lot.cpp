#include "gatekeeper/domain/idempotency/parking_lot.h"

#include <utility>

namespace gatekeeper::domain::idempotency
{

bool ParkingLot::Park(std::string_view key, std::uint64_t waiter_id, WakeCallback callback, std::uint64_t timeout_at_ms)
{
    if (key.empty() || waiter_id == 0 || !callback)
    {
        return false;
    }

    const std::lock_guard lock(mutex_);
    auto& list = waiters_[std::string(key)];
    for (const auto& w : list)
    {
        if (w.waiter_id == waiter_id)
        {
            return false;
        }
    }

    list.push_back(Waiter{waiter_id, std::move(callback), timeout_at_ms});
    return true;
}

std::size_t ParkingLot::BroadcastAndUnpark(std::string_view key, int code, std::string_view body)
{
    std::vector<Waiter> to_wake;
    {
        const std::lock_guard lock(mutex_);
        auto it = waiters_.find(std::string(key));
        if (it == waiters_.end())
        {
            return 0;
        }
        to_wake = std::move(it->second);
        waiters_.erase(it);
    }

    for (const auto& waiter : to_wake)
    {
        if (waiter.callback)
        {
            waiter.callback(code, body, false);
        }
    }

    return to_wake.size();
}

bool ParkingLot::Cancel(std::string_view key, std::uint64_t waiter_id)
{
    const std::lock_guard lock(mutex_);
    auto it = waiters_.find(std::string(key));
    if (it == waiters_.end())
    {
        return false;
    }

    auto& list = it->second;
    for (auto vit = list.begin(); vit != list.end(); ++vit)
    {
        if (vit->waiter_id == waiter_id)
        {
            list.erase(vit);
            if (list.empty())
            {
                waiters_.erase(it);
            }
            return true;
        }
    }

    return false;
}

std::size_t ParkingLot::PurgeTimeout(std::uint64_t now_ms)
{
    std::vector<Waiter> timed_out;
    {
        const std::lock_guard lock(mutex_);
        std::vector<std::string> empty_keys;

        for (auto& [key, list] : waiters_)
        {
            for (auto it = list.begin(); it != list.end();)
            {
                if (it->timeout_at_ms > 0 && now_ms >= it->timeout_at_ms)
                {
                    timed_out.push_back(std::move(*it));
                    it = list.erase(it);
                }
                else
                {
                    ++it;
                }
            }
            if (list.empty())
            {
                empty_keys.push_back(key);
            }
        }

        for (const auto& k : empty_keys)
        {
            waiters_.erase(k);
        }
    }

    for (const auto& waiter : timed_out)
    {
        if (waiter.callback)
        {
            waiter.callback(504, "{\"error\":{\"code\":\"ERR_GATEWAY_TIMEOUT\",\"message\":\"Upstream execution timed out while parked\"}}", true);
        }
    }

    return timed_out.size();
}

std::size_t ParkingLot::WaiterCount(std::string_view key) const
{
    const std::lock_guard lock(mutex_);
    auto it = waiters_.find(std::string(key));
    return it != waiters_.end() ? it->second.size() : 0;
}

std::size_t ParkingLot::TotalWaiters() const
{
    const std::lock_guard lock(mutex_);
    std::size_t total = 0;
    for (const auto& [_, list] : waiters_)
    {
        total += list.size();
    }
    return total;
}

void ParkingLot::Clear()
{
    const std::lock_guard lock(mutex_);
    waiters_.clear();
}

}
