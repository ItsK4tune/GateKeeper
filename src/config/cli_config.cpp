#include "gatekeeper/config/cli_config.h"

#include <stdexcept>

namespace gatekeeper::config
{

CliConfig CliConfig::Parse(int argc, const char* const* argv)
{
    CliConfig config;
    bool has_host = false;
    bool has_port = false;
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument = argv[index];
        if (config.command)
        {
            *config.command += ' ';
            *config.command += argument;
        }
        else if (argument == "--help")
        {
            config.show_help = true;
        }
        else if (argument == "-h" || argument == "--host")
        {
            config.host = config::ReadOptionValue(argc, argv, index, "host", has_host);
            if (config.host.empty())
            {
                throw std::invalid_argument("INVALID_HOST: host cannot be empty");
            }
        }
        else if (argument == "-p" || argument == "--port")
        {
            config.port = config::ParsePort(config::ReadOptionValue(argc, argv, index, "port", has_port));
        }
        else if (argument.starts_with('-'))
        {
            throw std::invalid_argument("INVALID_OPTION: unknown option: " + std::string(argument) +
                                        ". Use gate --help.");
        }
        else
        {
            config.command = argument;
        }
    }
    return config;
}

std::string_view CliConfig::Usage()
{
    return "Usage: gate [-h host | --host host] [-p port | --port port] [command]\n"
           "  -h, --host  Server host (default: 127.0.0.1)\n"
           "  -p, --port  TCP port (default: 63779, range: 1..65535)\n"
           "  --help      Show this help\n";
}

}
