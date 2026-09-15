#include "gatekeeper/cli/application.h"
#include "gatekeeper/cli/tcp_client.h"

#include <charconv>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

int main(int argc, char* argv[])
{
    try
    {
        std::string host = "127.0.0.1";
        int port = 63779;
        std::optional<std::string> command;
        for (int index = 1; index < argc; ++index)
        {
            const std::string argument = argv[index];
            if (!command && (argument == "-h" || argument == "-p"))
            {
                if (++index == argc)
                {
                    throw std::invalid_argument("missing value for " + argument);
                }
                const std::string value = argv[index];
                if (argument == "-h")
                {
                    host = value;
                }
                else
                {
                    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), port);
                    if (error != std::errc{} || end != value.data() + value.size() || port < 1 || port > 65535)
                    {
                        throw std::invalid_argument("port must be between 1 and 65535");
                    }
                }
            }
            else if (!command && argument == "--help")
            {
                std::cout << "Usage: gate [-h host] [-p port] [command]\n";
                return 0;
            }
            else if (!command && argument.starts_with('-'))
            {
                throw std::invalid_argument("unknown option: " + argument);
            }
            else if (command)
            {
                *command += ' ' + argument;
            }
            else
            {
                command = argument;
            }
        }
        gatekeeper::cli::TcpClient client(host, static_cast<std::uint16_t>(port));
        gatekeeper::cli::CommandRegistry commands([&client](const std::string& operation) {
            return client.Execute(operation);
        });
        gatekeeper::cli::Application application(commands);
        return command ? application.RunCommand(*command, std::cout)
                       : application.RunRepl(std::cin, std::cout);
    }
    catch (const std::exception& error)
    {
        std::cerr << "gate: " << error.what() << '\n';
        return 1;
    }
}
