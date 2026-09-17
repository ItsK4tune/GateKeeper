#pragma once

#include "gatekeeper/command/result.h"
#include "gatekeeper/log/logger.h"
#include "gatekeeper/protocol/request.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace gatekeeper::service
{

class Processor
{
public:
    using Dispatch = std::function<command::Result(const protocol::Request&)>;

    explicit Processor(Dispatch dispatch, std::shared_ptr<log::Logger> logger = log::Logger::Null());
    std::string Process(std::string_view payload) const;

private:
    Dispatch dispatch_;
    std::shared_ptr<log::Logger> logger_;
};

using RequestProcessor = Processor;

}
