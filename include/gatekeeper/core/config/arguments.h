#pragma once

#include <string_view>

namespace gatekeeper::config
{

inline constexpr int kDefaultPort = 63779;

int ParsePort(std::string_view value);
std::string_view ReadOptionValue(int argc, const char* const* argv, int& index,
                                std::string_view name, bool& already_seen);

}
