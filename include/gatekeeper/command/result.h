#pragma once

#include "gatekeeper/protocol/gkwp/error.h"

#include <string>

namespace gatekeeper::command
{

struct Result
{
    bool ok = false;
    std::string result_json;
    protocol::Error error;
};

}
