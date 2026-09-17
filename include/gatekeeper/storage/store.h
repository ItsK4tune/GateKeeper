#pragma once

#include "gatekeeper/storage/entry.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gatekeeper::storage
{

enum class WriteCondition
{
    Always,
    IfAbsent,
    IfPresent,
};

class Store
{
public:
    virtual ~Store() = default;

    virtual bool Set(std::string key, std::string value, WriteCondition condition) = 0;
    virtual std::optional<std::string> Get(std::string_view key) const = 0;

    virtual bool Del(std::string_view key) = 0;
    virtual bool Exists(std::string_view key) const = 0;
    virtual DataType Type(std::string_view key) const = 0;
    virtual std::size_t DbSize() const = 0;
    virtual std::vector<std::string> Keys(std::string_view pattern) const = 0;
    virtual std::pair<std::size_t, std::vector<std::string>> Scan(std::size_t cursor, std::size_t count) const = 0;
};

using StringStore = Store;

}
