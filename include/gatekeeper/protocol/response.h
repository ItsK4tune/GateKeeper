#pragma once

#include "gatekeeper/protocol/error.h"

#include <string>
#include <string_view>

namespace gatekeeper::protocol
{

std::string QuoteJson(std::string_view value);
std::string EncodeSuccessResponse(const std::string& id, const std::string& result_json);
std::string EncodeErrorResponse(const std::string& id, const ProtocolError& error);

}
