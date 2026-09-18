#include "gatekeeper/core/config/arguments.h"

#include <charconv>
#include <stdexcept>
#include <string>

namespace gatekeeper::config
{

int ParsePort(std::string_view value)
{
    int port = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), port);
    if (error != std::errc{} || end != value.data() + value.size() || port < 1 || port > 65535)
    {
        throw std::invalid_argument("INVALID_PORT: port must be between 1 and 65535");
    }
    return port;
}

std::string_view ReadOptionValue(int argc, const char* const* argv, int& index,
                                std::string_view name, bool& already_seen)
{
    if (already_seen)
    {
        throw std::invalid_argument("INVALID_OPTION: " + std::string(name) + " may only be specified once");
    }
    const std::string option = argv[index];
    if (++index == argc)
    {
        throw std::invalid_argument("INVALID_OPTION: missing value for " + option);
    }
    already_seen = true;
    return argv[index];
}

}
