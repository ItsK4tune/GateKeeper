#include "gatekeeper/service/processor.h"
#include "gatekeeper/protocol/parser.h"
#include "gatekeeper/protocol/response.h"

#include <stdexcept>
#include <utility>

namespace gatekeeper::service
{

Processor::Processor(Dispatch dispatch, std::shared_ptr<log::Logger> logger)
    : dispatch_(std::move(dispatch)), logger_(std::move(logger))
{
    if (!logger_)
    {
        logger_ = log::Logger::Null();
    }
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
        logger_->Warn("Failed to parse request: " + error.code + " - " + error.message);
        return protocol::EncodeErrorResponse(request.id, error);
    }
    logger_->Info("Executing request id=\"" + request.id + "\" op=\"" + request.op + "\"");
    const auto result = dispatch_(request);
    if (result.ok)
    {
        logger_->Info("Request id=\"" + request.id + "\" completed successfully");
        return protocol::EncodeSuccessResponse(request.id, result.result_json);
    }
    logger_->Warn("Request id=\"" + request.id + "\" failed: " + result.error.code + " - " + result.error.message);
    return protocol::EncodeErrorResponse(request.id, result.error);
}

}
