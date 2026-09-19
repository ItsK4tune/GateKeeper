#include "gatekeeper/storage/aof/aof_writer.h"
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
namespace {
bool IsWriteOp(std::string_view op)
{
    return op == "SET" || op == "DEL" || op == "EXPIRE" || op == "PEXPIRE" || op == "PERSIST" ||
           op == "GK.RATE_LIMIT" || op == "GK.RESERVE" || op == "GK.COMMIT" || op == "GK.ROLLBACK" ||
           op == "GK.QUOTA_INIT" || op == "GK.IDEM_BEGIN" || op == "GK.IDEM_COMPLETE" || op == "GK.IDEM_FAIL";
}
}


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
    auto res = handler->second(request);
    if (res.ok && aof_writer_ && IsWriteOp(request.op))
    {
        aof_writer_->Append(request.op, request.body_json);
    }
    return res;
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

void Dispatcher::SetAofWriter(std::shared_ptr<storage::aof::AofWriter> writer)
{
    aof_writer_ = std::move(writer);
}

std::shared_ptr<storage::aof::AofWriter> Dispatcher::GetAofWriter() const noexcept
{
    return aof_writer_;
}


}
