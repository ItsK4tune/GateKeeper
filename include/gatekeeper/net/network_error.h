#pragma once

#include <string>

namespace gatekeeper::net
{

std::string DescribeNetworkError(const std::string& operation, const std::string& endpoint, int error);

}
