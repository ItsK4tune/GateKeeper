#pragma once

#include "gatekeeper/protocol/resp/decoder.h"

#include <string>
#include <string_view>
#include <vector>

namespace gatekeeper::protocol::resp
{

std::string CommandToJsonRequest(const Command& cmd, std::uint64_t req_id, std::string& out_op);

std::string FormatRespResponse(std::string_view op, std::string_view json_resp, int resp_version);

std::string FormatHelloResponse(int version);

std::string FormatSimpleString(std::string_view str);
std::string FormatError(std::string_view msg);
std::string FormatInteger(long long val);
std::string FormatBulkString(std::string_view str);
std::string FormatNull(int resp_version);

}
