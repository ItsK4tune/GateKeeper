#pragma once

#include "gatekeeper/protocol/gkwp2/header.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

namespace gatekeeper::protocol::gkwp2
{

enum class StreamState : std::uint8_t
{
    Idle,
    Open,
    HalfClosedRemote,
    HalfClosedLocal,
    Closed,
};

struct Stream
{
    std::uint32_t id{0};
    StreamState state{StreamState::Idle};
    std::uint64_t last_activity_ms{0};
    std::string fragment_buffer;
};

class StreamManager
{
public:
    explicit StreamManager(std::uint32_t max_concurrent_streams = 1024, std::uint64_t idle_timeout_ms = 30000)
        : max_concurrent_streams_(max_concurrent_streams), idle_timeout_ms_(idle_timeout_ms)
    {
    }

    bool CanOpenStream(std::uint32_t stream_id) const;
    Stream* GetOrCreateStream(std::uint32_t stream_id, std::uint64_t now_ms);
    Stream* FindStream(std::uint32_t stream_id);
    void CloseStream(std::uint32_t stream_id);
    std::size_t PurgeIdleStreams(std::uint64_t now_ms, const std::function<void(std::uint32_t)>& on_timeout = nullptr);
    [[nodiscard]] std::size_t ActiveStreamCount() const noexcept { return streams_.size(); }

private:
    std::uint32_t max_concurrent_streams_;
    std::uint64_t idle_timeout_ms_;
    std::unordered_map<std::uint32_t, Stream> streams_;
};

}
