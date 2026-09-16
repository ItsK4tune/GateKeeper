#include "gatekeeper/cli/options.h"

#include <stdexcept>

namespace gatekeeper::cli
{

Options Options::Parse(int argc, const char* const* argv)
{
    Options options;
    bool has_host = false;
    bool has_port = false;
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument = argv[index];
        if (options.command)
        {
            *options.command += ' ';
            *options.command += argument;
        }
        else if (argument == "--help")
        {
            options.show_help = true;
        }
        else if (argument == "-h" || argument == "--host")
        {
            options.host = config::ReadOptionValue(argc, argv, index, "host", has_host);
            if (options.host.empty())
            {
                throw std::invalid_argument("INVALID_HOST: host cannot be empty");
            }
        }
        else if (argument == "-p" || argument == "--port")
        {
            options.port = config::ParsePort(config::ReadOptionValue(argc, argv, index, "port", has_port));
        }
        else if (argument.starts_with('-'))
        {
            throw std::invalid_argument("INVALID_OPTION: unknown option: " + std::string(argument) +
                                        ". Use gate --help.");
        }
        else
        {
            options.command = argument;
        }
    }
    return options;
}

std::string_view Options::Usage()
{
    return "Usage: gate [-h host | --host host] [-p port | --port port] [command]\n"
           "  -h, --host  Server host (default: 127.0.0.1)\n"
           "  -p, --port  TCP port (default: 63779, range: 1..65535)\n"
           "  --help      Show this help\n";
}

}
