#include "gatekeeper/domain/lock/lock_ops.h"
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
           op == "GK.QUOTA_INIT" || op == "GK.IDEM_BEGIN" || op == "GK.IDEM_COMPLETE" || op == "GK.IDEM_FAIL" || op == "GK.LOCK_ACQUIRE" || op == "GK.LOCK_RELEASE" || op == "GK.LOCK_EXTEND";
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
    RegisterLockOps(*this, *store_);
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
        if (request.op == "GK.IDEM_BEGIN")
        {
            std::string aof_body = request.body_json;
            if (aof_body.find("\"owner_token\"") == std::string::npos)
            {
                auto token_pos = res.result_json.find("\"owner_token\":\"");
                if (token_pos != std::string::npos)
                {
                    token_pos += 15;
                    auto token_end = res.result_json.find('"', token_pos);
                    if (token_end != std::string::npos)
                    {
                        auto token = res.result_json.substr(token_pos, token_end - token_pos);
                        auto last_brace = aof_body.rfind('}');
                        if (last_brace != std::string::npos)
                        {
                            aof_body.insert(last_brace, ",\"owner_token\":\"" + token + "\"");
                        }
                    }
                }
            }
            aof_writer_->Append(request.op, aof_body);
        }
        else
        {
            aof_writer_->Append(request.op, request.body_json);
        }
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
