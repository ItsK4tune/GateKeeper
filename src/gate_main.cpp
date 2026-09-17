#include "gatekeeper/cli/application.h"
#include "gatekeeper/cli/options.h"
#include "gatekeeper/cli/tcp_client.h"

#include <iostream>

int main(int argc, char* argv[])
{
    try
    {
        const auto options = gatekeeper::cli::Options::Parse(argc, argv);
        if (options.show_help)
        {
            std::cout << gatekeeper::cli::Options::Usage();
            return 0;
        }
        gatekeeper::cli::TcpClient client(options.host, static_cast<std::uint16_t>(options.port));
        gatekeeper::cli::CommandRegistry commands([&client](const std::string& operation, const std::string& body) {
            return client.Execute(operation, body);
        });
        gatekeeper::cli::Application application(commands, [&client]() { client.Open(); });
        return options.command ? application.RunCommand(*options.command, std::cout)
                               : application.RunRepl(std::cin, std::cout);
    }
    catch (const std::exception& error)
    {
        std::cerr << "gate: " << error.what() << '\n';
        return 1;
    }
}
