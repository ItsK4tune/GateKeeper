#pragma once

#include <map>
#include <string>
#include <string_view>
#include <variant>

namespace gatekeeper::command
{

using BodyValue = std::variant<std::string, bool>;
using StringBody = std::map<std::string, BodyValue>;

StringBody ParseStringBody(std::string_view json);

}

namespace gatekeeper::protocol
{
using BodyValue = command::BodyValue;
using StringBody = command::StringBody;
using command::ParseStringBody;
}
