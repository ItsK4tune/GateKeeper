#pragma once

#include <string>

namespace gatekeeper::protocol
{

struct ProtocolError
{
    std::string code;
    std::string message;
};

}
