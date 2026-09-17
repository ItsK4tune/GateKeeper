#pragma once

#include "gatekeeper/command/dispatch_result.h"
#include "gatekeeper/protocol/request.h"

#include <functional>
#include <string>
#include <string_view>

namespace gatekeeper::service
{

class Processor
{
public:
    using Dispatch = std::function<command::DispatchResult(const protocol::Request&)>;

    explicit Processor(Dispatch dispatch);
    std::string Process(std::string_view payload) const;

private:
    Dispatch dispatch_;
};

using RequestProcessor = Processor;

}
