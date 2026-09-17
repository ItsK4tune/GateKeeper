#pragma once

#include "gatekeeper/storage/store.h"

#include <map>
#include <mutex>

namespace gatekeeper::storage
{

class MemoryStore final : public Store
{
public:
    bool Set(std::string key, std::string value, WriteCondition condition) override;
    std::optional<std::string> Get(std::string_view key) const override;

private:
    mutable std::mutex mutex_;
    std::map<std::string, std::string, std::less<>> values_;
};

using InMemoryStringStore = MemoryStore;

}
