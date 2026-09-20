#pragma once

#include "gatekeeper/domain/lock/lock_types.h"
#include "gatekeeper/storage/store.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace gatekeeper::domain::lock
{

class LockWaitQueue
{
public:
    struct Waiter
    {
        std::uint64_t waiter_id{0};
        std::uint64_t ttl_ms{0};
        std::shared_ptr<std::mutex> cv_mutex;
        std::shared_ptr<std::condition_variable> cv;
        std::shared_ptr<bool> granted;
        std::shared_ptr<LockAcquireResult> result;
        std::uint64_t timeout_at_ms{0};
    };

    LockWaitQueue() = default;
    ~LockWaitQueue() = default;

    LockWaitQueue(const LockWaitQueue&) = delete;
    LockWaitQueue& operator=(const LockWaitQueue&) = delete;

    LockAcquireResult WaitOrAcquire(
        storage::Store& store,
        std::string_view resource,
        std::uint64_t ttl_ms,
        std::uint64_t max_wait_ms = 0,
        std::string_view owner_token = "",
        std::uint64_t session_id = 0,
        bool is_ephemeral = false);

    bool WakeNext(storage::Store& store, std::string_view resource);
    void WakeAll(storage::Store& store, std::string_view resource);

    std::size_t QueueLength(std::string_view resource) const;
    std::size_t TotalWaiters() const;
    void Clear();

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::deque<Waiter>> queues_;
    std::atomic<std::uint64_t> next_waiter_id_{1};
};

LockWaitQueue& GetGlobalLockWaitQueue();

}
