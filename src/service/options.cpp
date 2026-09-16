#include "gatekeeper/service/options.h"

#include <stdexcept>
#include <string>
#include <string_view>

namespace gatekeeper::service
{

Options Options::Parse(int argc, const char* const* argv)
{
    Options options;
    bool has_port = false;
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument = argv[index];
        if (argument == "--help" || argument == "-h")
        {
            options.show_help = true;
            continue;
        }
        if (argument != "--port" && argument != "-p")
        {
            throw std::invalid_argument("INVALID_OPTION: unknown option: " + std::string(argument) +
                                        ". Use gatekeeper --help.");
        }
        options.port = config::ParsePort(config::ReadOptionValue(argc, argv, index, "port", has_port));
    }
    return options;
}

std::string_view Options::Usage()
{
    return "Usage: gatekeeper [-p port | --port port] [--help]\n"
           "  -p, --port  TCP listening port (default: 63779, range: 1..65535)\n"
           "  -h, --help  Show this help\n";
}

}
