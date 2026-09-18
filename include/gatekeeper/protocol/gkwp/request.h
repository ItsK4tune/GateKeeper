#pragma once

#include "gatekeeper/protocol/gkwp/error.h"

#include <string>

namespace gatekeeper::protocol
{

struct Request
{
    std::string id;
    std::string op;
    std::string body_json;
};

}
