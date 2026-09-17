#pragma once

#include "gatekeeper/protocol/request.h"
#include "gatekeeper/command/dispatch_result.h"

#include <string>
#include <functional>
#include <map>
#include <memory>

namespace gatekeeper::storage
{
class StringStore;
}

namespace gatekeeper::command
{

class CommandDispatcher
{
public:
    using Handler = std::function<DispatchResult(const protocol::Request&)>;

    CommandDispatcher();
    explicit CommandDispatcher(std::unique_ptr<storage::StringStore> store);
    ~CommandDispatcher();
    void Register(std::string name, Handler handler);
    DispatchResult Dispatch(const protocol::Request& request) const;

private:
    std::unique_ptr<storage::StringStore> store_;
    std::map<std::string, Handler> commands_;
};

}
