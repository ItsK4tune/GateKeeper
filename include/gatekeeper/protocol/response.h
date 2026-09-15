#pragma once

#include "gatekeeper/protocol/request.h"

#include <string>

namespace gatekeeper::protocol
{

std::string EncodeSuccessResponse(const std::string& id, const std::string& result_json);
std::string EncodeErrorResponse(const std::string& id, const ProtocolError& error);

}
