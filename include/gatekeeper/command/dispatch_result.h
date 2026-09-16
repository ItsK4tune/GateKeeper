#pragma once

#include "gatekeeper/protocol/error.h"

#include <string>

namespace gatekeeper::command
{

struct DispatchResult
{
    bool ok;
    std::string result_json;
    protocol::ProtocolError error;
};

}
