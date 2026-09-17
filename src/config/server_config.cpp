#include "gatekeeper/config/server_config.h"

#include <stdexcept>
#include <string>
#include <string_view>

namespace gatekeeper::config
{

ServerConfig ServerConfig::Parse(int argc, const char* const* argv)
{
    ServerConfig config;
    bool has_port = false;
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument = argv[index];
        if (argument == "--help" || argument == "-h")
        {
            config.show_help = true;
            continue;
        }
        if (argument != "--port" && argument != "-p")
        {
            throw std::invalid_argument("INVALID_OPTION: unknown option: " + std::string(argument) +
                                        ". Use gatekeeper --help.");
        }
        config.port = config::ParsePort(config::ReadOptionValue(argc, argv, index, "port", has_port));
    }
    return config;
}

std::string_view ServerConfig::Usage()
{
    return "Usage: gatekeeper [-p port | --port port] [--help]\n"
           "  -p, --port  TCP listening port (default: 63779, range: 1..65535)\n"
           "  -h, --help  Show this help\n";
}

}
