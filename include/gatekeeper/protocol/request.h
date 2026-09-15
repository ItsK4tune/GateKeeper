#pragma once

#include <string>

namespace gatekeeper::protocol
{

struct Request
{
    std::string id;
    std::string op;
    std::string body_json;
};

struct ProtocolError
{
    std::string code;
    std::string message;
};

}
