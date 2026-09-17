#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace gatekeeper::storage
{

enum class WriteCondition
{
    Always,
    IfAbsent,
    IfPresent,
};

class StringStore
{
public:
    virtual ~StringStore() = default;

    virtual bool Set(std::string key, std::string value, WriteCondition condition) = 0;
    virtual std::optional<std::string> Get(std::string_view key) const = 0;
};

}
