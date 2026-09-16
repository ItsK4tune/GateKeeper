#pragma once

#include "gatekeeper/protocol/request.h"
#include "gatekeeper/command/dispatch_result.h"

#include <string>
#include <functional>
#include <map>

namespace gatekeeper::command
{

class CommandDispatcher
{
public:
    using Handler = std::function<DispatchResult(const protocol::Request&)>;

    CommandDispatcher();
    void Register(std::string name, Handler handler);
    DispatchResult Dispatch(const protocol::Request& request) const;

private:
    std::map<std::string, Handler> commands_;
};

}
