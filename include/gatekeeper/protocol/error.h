#pragma once

#include <string>

namespace gatekeeper::protocol
{

struct Error
{
    std::string code;
    std::string message;
};

using ProtocolError = Error;

}
