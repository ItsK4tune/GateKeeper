#include "gatekeeper/cli/app.h"
#include "gatekeeper/config/cli_config.h"
#include "gatekeeper/net/client.h"

#include <iostream>

int main(int argc, char* argv[])
{
    try
    {
        const auto config = gatekeeper::config::CliConfig::Parse(argc, argv);
        if (config.show_help)
        {
            std::cout << gatekeeper::config::CliConfig::Usage();
            return 0;
        }
        gatekeeper::net::Client client(config.host, static_cast<std::uint16_t>(config.port));
        gatekeeper::cli::Registry commands([&client](const std::string& operation, const std::string& body) {
            return client.Execute(operation, body);
        });
        gatekeeper::cli::App application(commands, [&client]() { client.Open(); });
        return config.command ? application.RunCommand(*config.command, std::cout)
                              : application.RunRepl(std::cin, std::cout);
    }
    catch (const std::exception& error)
    {
        std::cerr << "gate: " << error.what() << '\n';
        return 1;
    }
}
