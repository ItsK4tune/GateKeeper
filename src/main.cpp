#include "gatekeeper/net/tcp_server.h"
#include "gatekeeper/service/options.h"
#include "gatekeeper/service/request_processor.h"
#include "gatekeeper/command/command_dispatcher.h"

#include <iostream>

int main(int argc, char* argv[])
{
    try
    {
        const auto options = gatekeeper::service::Options::Parse(argc, argv);
        if (options.show_help)
        {
            std::cout << gatekeeper::service::Options::Usage();
            return 0;
        }
        gatekeeper::command::CommandDispatcher dispatcher;
        gatekeeper::service::RequestProcessor processor([&dispatcher](const auto& request) {
            return dispatcher.Dispatch(request);
        });
        gatekeeper::net::TcpServer server(options.port, [&processor](std::string_view payload) {
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
