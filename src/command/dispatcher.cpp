#include "gatekeeper/domain/idempotency/idempotency_ops.h"
#include "gatekeeper/command/dispatcher.h"
#include "gatekeeper/domain/ratelimit/counter_ops.h"
#include "gatekeeper/domain/kv/key_ops.h"
#include "gatekeeper/command/name.h"
#include "gatekeeper/domain/quota/reservation_ops.h"
#include "gatekeeper/domain/kv/string_ops.h"
#include "gatekeeper/domain/kv/ttl_ops.h"
#include "gatekeeper/storage/memory_store.h"

#include <stdexcept>
#include <utility>

namespace gatekeeper::command
{

Dispatcher::Dispatcher()
    : Dispatcher(std::make_unique<storage::MemoryStore>())
{
}

Dispatcher::Dispatcher(std::unique_ptr<storage::Store> store)
    : store_(std::move(store))
{
    if (!store_)
    {
        throw std::invalid_argument("string store is required");
    }
    Register("PING", [](const protocol::Request&) {
        return Result{true, R"({"pong":true})", {}};
    });
    RegisterStringOps(*this, *store_);
    RegisterKeyOps(*this, *store_);
    RegisterTtlOps(*this, *store_);
    RegisterCounterOps(*this, *store_);
    RegisterReservationOps(*this, *store_);
    RegisterIdempotencyOps(*this, *store_);
}

Dispatcher::~Dispatcher() = default;

void Dispatcher::Register(std::string name, Handler handler)
{
    name = NormalizeName(name);
    if (name.empty() || !handler || commands_.contains(name))
    {
        throw std::invalid_argument("invalid or duplicate command registration");
    }
    commands_.emplace(std::move(name), std::move(handler));
}

Result Dispatcher::Dispatch(const protocol::Request& request) const
{
    const auto handler = commands_.find(NormalizeName(request.op));
    if (handler == commands_.end())
    {
        return {false, {}, {"UNKNOWN_COMMAND", "unsupported command: " + request.op}};
    }
    return handler->second(request);
}

std::size_t Dispatcher::PurgeExpired(std::size_t sample_limit)
{
    return store_ ? store_->PurgeExpired(sample_limit) : 0;
}

storage::Store& Dispatcher::GetStore() noexcept
{
    return *store_;
}

const storage::Store& Dispatcher::GetStore() const noexcept
{
    return *store_;
}

}
