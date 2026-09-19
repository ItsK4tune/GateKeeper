namespace gatekeeper::storage::aof { class AofWriter; }
#pragma once

#include "gatekeeper/command/result.h"
#include "gatekeeper/protocol/request.h"
#include "gatekeeper/storage/store.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace gatekeeper::command
{

class Dispatcher
{
public:
    using Handler = std::function<Result(const protocol::Request&)>;

    Dispatcher();
    explicit Dispatcher(std::unique_ptr<storage::Store> store);
    ~Dispatcher();

    Dispatcher(const Dispatcher&) = delete;
    Dispatcher& operator=(const Dispatcher&) = delete;

    void Register(std::string name, Handler handler);
    Result Dispatch(const protocol::Request& request) const;
    void SetAofWriter(std::shared_ptr<storage::aof::AofWriter> writer);
    [[nodiscard]] std::shared_ptr<storage::aof::AofWriter> GetAofWriter() const noexcept;
    std::size_t PurgeExpired(std::size_t sample_limit = 50);

    [[nodiscard]] storage::Store& GetStore() noexcept;
    [[nodiscard]] const storage::Store& GetStore() const noexcept;

private:
    std::unique_ptr<storage::Store> store_;
    std::shared_ptr<storage::aof::AofWriter> aof_writer_;
    std::unordered_map<std::string, Handler> commands_;
};

}
