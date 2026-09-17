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
    bool has_log = false;
    bool has_log_dir = false;
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument = argv[index];
        if (argument == "--help" || argument == "-h")
        {
            config.show_help = true;
            continue;
        }
        if (argument == "--port" || argument == "-p")
        {
            config.port = config::ParsePort(config::ReadOptionValue(argc, argv, index, "port", has_port));
        }
        else if (argument == "--log" || argument == "-l")
        {
            const auto value = config::ReadOptionValue(argc, argv, index, "log", has_log);
            if (value == "terminal" || value == "console")
            {
                config.log_mode = log::Mode::Terminal;
            }
            else if (value == "file")
            {
                config.log_mode = log::Mode::File;
            }
            else if (value == "none")
            {
                config.log_mode = log::Mode::None;
            }
            else
            {
                throw std::invalid_argument("INVALID_OPTION: invalid log mode: " + std::string(value) +
                                            ". Expected terminal, file, or none.");
            }
        }
        else if (argument == "--log-dir" || argument == "-d")
        {
            config.log_dir = config::ReadOptionValue(argc, argv, index, "log-dir", has_log_dir);
            if (!has_log)
            {
                config.log_mode = log::Mode::File;
            }
        }
        else
        {
            throw std::invalid_argument("INVALID_OPTION: unknown option: " + std::string(argument) +
                                        ". Use gatekeeper --help.");
        }
    }
    return config;
}

std::string_view ServerConfig::Usage()
{
    return "Usage: gatekeeper [-p port | --port port] [-l mode | --log mode] [-d dir | --log-dir dir] [--help]\n"
           "  -p, --port     TCP listening port (default: 63779, range: 1..65535)\n"
           "  -l, --log      Logging mode: none, terminal, file (default: none)\n"
           "  -d, --log-dir  Directory to write log file (<dir>/log)\n"
           "  -h, --help     Show this help\n";
}

}
