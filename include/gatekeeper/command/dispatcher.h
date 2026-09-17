#pragma once

#include "gatekeeper/command/dispatch_result.h"
#include "gatekeeper/protocol/request.h"
#include "gatekeeper/storage/store.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace gatekeeper::command
{

class Dispatcher
{
public:
    using Handler = std::function<DispatchResult(const protocol::Request&)>;

    Dispatcher();
    explicit Dispatcher(std::unique_ptr<storage::Store> store);
    ~Dispatcher();

    Dispatcher(const Dispatcher&) = delete;
    Dispatcher& operator=(const Dispatcher&) = delete;

    void Register(std::string name, Handler handler);
    DispatchResult Dispatch(const protocol::Request& request) const;

private:
    std::unique_ptr<storage::Store> store_;
    std::unordered_map<std::string, Handler> commands_;
};

using CommandDispatcher = Dispatcher;

}
