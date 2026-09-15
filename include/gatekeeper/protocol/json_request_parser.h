#pragma once

#include "gatekeeper/protocol/request.h"

#include <string_view>

namespace gatekeeper::protocol
{

class JsonRequestParser
{
public:
    bool Parse(std::string_view payload, Request& request, ProtocolError& error) const;
};

}
