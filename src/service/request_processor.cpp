#include "gatekeeper/service/request_processor.h"

#include "gatekeeper/protocol/json_request_parser.h"
#include "gatekeeper/protocol/response.h"

#include <stdexcept>
#include <utility>

namespace gatekeeper::service
{

RequestProcessor::RequestProcessor(Dispatch dispatch) : dispatch_(std::move(dispatch))
{
    if (!dispatch_)
    {
        throw std::invalid_argument("request dispatcher is required");
    }
}

std::string RequestProcessor::Process(std::string_view payload) const
{
    protocol::Request request;
    protocol::ProtocolError error;
    const protocol::JsonRequestParser parser;
    if (!parser.Parse(payload, request, error))
    {
        return protocol::EncodeErrorResponse(request.id, error);
    }
    const auto result = dispatch_(request);
    return result.ok ? protocol::EncodeSuccessResponse(request.id, result.result_json)
                     : protocol::EncodeErrorResponse(request.id, result.error);
}

}
