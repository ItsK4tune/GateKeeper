#include "gatekeeper/config/server_config.h"
#include "gatekeeper/command/dispatcher.h"
#include "gatekeeper/net/server.h"
#include "gatekeeper/service/processor.h"

#include <iostream>

int main(int argc, char* argv[])
{
    try
    {
        const auto config = gatekeeper::config::ServerConfig::Parse(argc, argv);
        if (config.show_help)
        {
            std::cout << gatekeeper::config::ServerConfig::Usage();
            return 0;
        }
        gatekeeper::command::Dispatcher dispatcher;
        gatekeeper::service::Processor processor([&dispatcher](const auto& request) {
            return dispatcher.Dispatch(request);
        });
        gatekeeper::net::Server server(config.port, [&processor](std::string_view payload) {
            return processor.Process(payload);
        });
        server.Run();
    }
    catch (const std::exception& error)
    {
        std::cerr << "gatekeeper: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
