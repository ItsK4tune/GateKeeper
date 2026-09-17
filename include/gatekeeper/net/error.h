#pragma once

#include <string>

namespace gatekeeper::net
{

std::string DescribeError(const std::string& operation, const std::string& endpoint, int error);

inline std::string DescribeNetworkError(const std::string& operation, const std::string& endpoint, int error)
{
    return DescribeError(operation, endpoint, error);
}

}
