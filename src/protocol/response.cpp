#include "gatekeeper/protocol/response.h"
#include "gatekeeper/protocol/string_body.h"

namespace gatekeeper::protocol
{

std::string EncodeSuccessResponse(const std::string& id, const std::string& result_json)
{
    return "{\"id\":" + QuoteJson(id) + ",\"ok\":true,\"result\":" + result_json + "}";
}

std::string EncodeErrorResponse(const std::string& id, const ProtocolError& error)
{
    return "{\"id\":" + QuoteJson(id) + ",\"ok\":false,\"error\":{\"code\":" +
           QuoteJson(error.code) + ",\"message\":" + QuoteJson(error.message) + "}}";
}

}
