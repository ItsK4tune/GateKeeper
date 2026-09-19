#pragma once

#include "gatekeeper/protocol/error.h"
#include "gatekeeper/protocol/request.h"

#include <string_view>

namespace gatekeeper::protocol
{

class Parser
{
public:
    bool Parse(std::string_view payload, Request& request, Error& error) const;
};

}
