#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <variant>

namespace gatekeeper::command
{

using BodyValue = std::variant<std::string, bool, std::uint64_t>;
using StringBody = std::map<std::string, BodyValue>;

StringBody ParseStringBody(std::string_view json);

}
