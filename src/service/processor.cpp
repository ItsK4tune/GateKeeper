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
        const auto err_response = protocol::EncodeErrorResponse(request.id, error);
        logger_->Warn("Failed to parse request: " + error.code + " - " + error.message);
        logger_->Info("Response GKWP: " + err_response);
        return err_response;
    }
    logger_->Info("Executing request id=\"" + request.id + "\" op=\"" + request.op + "\"");
    const auto result = dispatch_(request);
    std::string response;
    if (result.ok)
    {
        response = protocol::EncodeSuccessResponse(request.id, result.result_json);
        logger_->Info("Response GKWP: " + response);
    }
    else
    {
        response = protocol::EncodeErrorResponse(request.id, result.error);
        logger_->Warn("Response GKWP: " + response);
    }
    return response;
}

}
