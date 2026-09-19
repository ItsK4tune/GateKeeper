#include "gatekeeper/protocol/gkwp2/stream_manager.h"
#include <vector>

namespace gatekeeper::protocol::gkwp2
{

bool StreamManager::CanOpenStream(std::uint32_t stream_id) const
{
    if (streams_.find(stream_id) != streams_.end())
    {
        return true;
    }
    return streams_.size() < max_concurrent_streams_;
}

Stream* StreamManager::GetOrCreateStream(std::uint32_t stream_id, std::uint64_t now_ms)
{
    auto it = streams_.find(stream_id);
    if (it != streams_.end())
    {
        it->second.last_activity_ms = now_ms;
        return &it->second;
    }

    if (streams_.size() >= max_concurrent_streams_)
    {
        return nullptr;
    }

    Stream stream{};
    stream.id = stream_id;
    stream.state = StreamState::Open;
    stream.last_activity_ms = now_ms;

    auto [inserted_it, _] = streams_.emplace(stream_id, std::move(stream));
    return &inserted_it->second;
}

Stream* StreamManager::FindStream(std::uint32_t stream_id)
{
    auto it = streams_.find(stream_id);
    return it != streams_.end() ? &it->second : nullptr;
}

void StreamManager::CloseStream(std::uint32_t stream_id)
{
    streams_.erase(stream_id);
}

std::size_t StreamManager::PurgeIdleStreams(std::uint64_t now_ms, const std::function<void(std::uint32_t)>& on_timeout)
{
    std::vector<std::uint32_t> to_remove;
    for (const auto& [id, stream] : streams_)
    {
        if (now_ms > stream.last_activity_ms && (now_ms - stream.last_activity_ms) >= idle_timeout_ms_)
        {
            to_remove.push_back(id);
        }
    }

    for (const auto id : to_remove)
    {
        if (on_timeout)
        {
            on_timeout(id);
        }
        streams_.erase(id);
    }
    return to_remove.size();
}

}
