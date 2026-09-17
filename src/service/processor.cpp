#include "gatekeeper/service/processor.h"
#include "gatekeeper/protocol/parser.h"
#include "gatekeeper/protocol/response.h"

#include <stdexcept>
#include <utility>

namespace gatekeeper::service
{

Processor::Processor(Dispatch dispatch) : dispatch_(std::move(dispatch))
{
    if (!dispatch_)
    {
        throw std::invalid_argument("request dispatcher is required");
    }
}

std::string Processor::Process(std::string_view payload) const
{
    protocol::Request request;
    protocol::Error error;
    const protocol::Parser parser;
    if (!parser.Parse(payload, request, error))
    {
        return protocol::EncodeErrorResponse(request.id, error);
    }
    const auto result = dispatch_(request);
    return result.ok ? protocol::EncodeSuccessResponse(request.id, result.result_json)
                     : protocol::EncodeErrorResponse(request.id, result.error);
}

}
