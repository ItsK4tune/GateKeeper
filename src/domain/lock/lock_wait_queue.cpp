#include "gatekeeper/domain/lock/lock_wait_queue.h"

#include <chrono>
#include <utility>

namespace gatekeeper::domain::lock
{

LockAcquireResult LockWaitQueue::WaitOrAcquire(
    storage::Store& store,
    std::string_view resource,
    std::uint64_t ttl_ms,
    std::uint64_t max_wait_ms,
    std::string_view owner_token,
    std::uint64_t session_id,
    bool is_ephemeral)
{
    // 1. First attempt direct acquire
    auto direct_res = store.LockAcquire(resource, ttl_ms, owner_token, session_id, is_ephemeral);
    if (direct_res.acquired || !direct_res.ok || max_wait_ms == 0)
    {
        return direct_res;
    }

    // 2. Lock is held, and caller is willing to wait up to max_wait_ms in fair FIFO order
    const auto waiter_id = next_waiter_id_++;
    const auto now = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    const auto timeout_at = now + max_wait_ms;

    auto cv_mutex = std::make_shared<std::mutex>();
    auto cv = std::make_shared<std::condition_variable>();
    auto granted = std::make_shared<bool>(false);
    auto result = std::make_shared<LockAcquireResult>();

    Waiter waiter;
    waiter.waiter_id = waiter_id;
    waiter.ttl_ms = ttl_ms;
    waiter.cv_mutex = cv_mutex;
    waiter.cv = cv;
    waiter.granted = granted;
    waiter.result = result;
    waiter.timeout_at_ms = timeout_at;

    {
        const std::lock_guard lock(mutex_);
        queues_[std::string(resource)].push_back(waiter);
    }

    // 3. Wait on condition_variable with timeout
    std::unique_lock ulock(*cv_mutex);
    const bool notified = cv->wait_for(ulock, std::chrono::milliseconds(max_wait_ms), [&]() {
        return *granted;
    });

    if (notified && *granted)
    {
        return *result;
    }

    // 4. Timed out waiting: cancel waiter from FIFO queue
    {
        const std::lock_guard lock(mutex_);
        auto it = queues_.find(std::string(resource));
        if (it != queues_.end())
        {
            auto& q = it->second;
            for (auto qit = q.begin(); qit != q.end(); ++qit)
            {
                if (qit->waiter_id == waiter_id)
                {
                    q.erase(qit);
                    break;
                }
            }
            if (q.empty())
            {
                queues_.erase(it);
            }
        }
    }

    LockAcquireResult timeout_res;
    timeout_res.ok = false;
    timeout_res.acquired = false;
    timeout_res.resource = std::string(resource);
    timeout_res.error_code = "ERR_LOCK_WAIT_TIMEOUT";
    timeout_res.error_message = "Lock wait timed out after " + std::to_string(max_wait_ms) + " ms";
    return timeout_res;
}

bool LockWaitQueue::WakeNext(storage::Store& store, std::string_view resource)
{
    Waiter next_waiter;
    bool found = false;

    {
        const std::lock_guard lock(mutex_);
        auto it = queues_.find(std::string(resource));
        if (it != queues_.end() && !it->second.empty())
        {
            next_waiter = it->second.front();
            it->second.pop_front();
            if (it->second.empty())
            {
                queues_.erase(it);
            }
            found = true;
        }
    }

    if (!found)
    {
        return false;
    }

    // Acquire lock on behalf of the next waiter
    auto acq_res = store.LockAcquire(resource, next_waiter.ttl_ms);
    {
        const std::lock_guard ulock(*next_waiter.cv_mutex);
        *next_waiter.result = std::move(acq_res);
        *next_waiter.granted = true;
    }
    next_waiter.cv->notify_one();
    return true;
}

void LockWaitQueue::WakeAll(storage::Store& store, std::string_view resource)
{
    while (WakeNext(store, resource))
    {
    }
}

std::size_t LockWaitQueue::QueueLength(std::string_view resource) const
{
    const std::lock_guard lock(mutex_);
    auto it = queues_.find(std::string(resource));
    return it != queues_.end() ? it->second.size() : 0;
}

std::size_t LockWaitQueue::TotalWaiters() const
{
    const std::lock_guard lock(mutex_);
    std::size_t total = 0;
    for (const auto& [_, q] : queues_)
    {
        total += q.size();
    }
    return total;
}

void LockWaitQueue::Clear()
{
    const std::lock_guard lock(mutex_);
    queues_.clear();
}

LockWaitQueue& GetGlobalLockWaitQueue()
{
    static LockWaitQueue global_queue;
    return global_queue;
}

}
