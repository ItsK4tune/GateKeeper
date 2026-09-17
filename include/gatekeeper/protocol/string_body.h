#pragma once
#include <map>
#include <string>
#include <string_view>
#include <variant>
namespace gatekeeper::protocol {
using BodyValue = std::variant<std::string, bool>;
using StringBody = std::map<std::string, BodyValue>;
StringBody ParseStringBody(std::string_view json);
std::string QuoteJson(std::string_view value);
}
