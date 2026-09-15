#pragma once

#include "gatekeeper/protocol/request.h"

#include <string>

namespace gatekeeper::command
{

struct DispatchResult
{
    bool ok;
    std::string result_json;
    protocol::ProtocolError error;
};

class CommandDispatcher
{
public:
    DispatchResult Dispatch(const protocol::Request& request) const;
};

}
