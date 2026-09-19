#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gatekeeper::domain::idempotency
{

class ParkingLot
{
public:
    using WakeCallback = std::function<void(int code, std::string_view body, bool timed_out)>;

    struct Waiter
    {
        std::uint64_t waiter_id{0};
        WakeCallback callback;
        std::uint64_t timeout_at_ms{0};
    };

    ParkingLot() = default;
    ~ParkingLot() = default;

    ParkingLot(const ParkingLot&) = delete;
    ParkingLot& operator=(const ParkingLot&) = delete;

    bool Park(std::string_view key, std::uint64_t waiter_id, WakeCallback callback, std::uint64_t timeout_at_ms = 0);
    std::size_t BroadcastAndUnpark(std::string_view key, int code, std::string_view body);
    bool Cancel(std::string_view key, std::uint64_t waiter_id);
    std::size_t PurgeTimeout(std::uint64_t now_ms);

    [[nodiscard]] std::size_t WaiterCount(std::string_view key) const;
    [[nodiscard]] std::size_t TotalWaiters() const;
    void Clear();

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::vector<Waiter>> waiters_;
};

}
