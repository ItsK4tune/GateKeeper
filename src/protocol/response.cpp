#include "gatekeeper/protocol/response.h"

namespace gatekeeper::protocol
{

std::string EncodeSuccessResponse(const std::string& id, const std::string& result_json)
{
    return "{\"id\":\"" + id + "\",\"ok\":true,\"result\":" + result_json + "}";
}

std::string EncodeErrorResponse(const std::string& id, const ProtocolError& error)
{
    return "{\"id\":\"" + id + "\",\"ok\":false,\"error\":{\"code\":\"" + error.code + "\",\"message\":\"" + error.message + "\"}}";
}

}
