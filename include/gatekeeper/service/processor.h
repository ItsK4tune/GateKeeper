#pragma once

#include "gatekeeper/command/result.h"
#include "gatekeeper/core/log/logger.h"
#include "gatekeeper/protocol/request.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace gatekeeper::storage { class Store; }

namespace gatekeeper::service
{

class Processor
{
public:
    using Dispatch = std::function<command::Result(const protocol::Request&)>;

    explicit Processor(Dispatch dispatch, std::shared_ptr<log::Logger> logger = log::Logger::Null(), storage::Store* store = nullptr);
    std::string Process(std::string_view payload) const;

private:
    std::string ProcessBinary(std::string_view payload) const;

    Dispatch dispatch_;
    std::shared_ptr<log::Logger> logger_;
    storage::Store* store_{nullptr};
};

}
