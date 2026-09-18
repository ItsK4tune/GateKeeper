#include "gatekeeper/config/server_config.h"
#include "gatekeeper/command/dispatcher.h"
#include "gatekeeper/log/logger.h"
#include "gatekeeper/net/server.h"
#include "gatekeeper/service/processor.h"
#include "gatekeeper/service/http_service.h"

#include <iostream>
#include <string>

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
        auto logger = gatekeeper::log::Logger::Create(config.log_mode, config.log_dir);

        gatekeeper::command::Dispatcher dispatcher;
        gatekeeper::service::Processor processor([&dispatcher](const auto& request) {
            return dispatcher.Dispatch(request);
        }, logger);
        gatekeeper::service::HttpService http_service(dispatcher.GetStore(), logger);

        gatekeeper::net::Server server(
            config.port,
            [&processor](std::string_view payload) {
                return processor.Process(payload);
            },
            logger,
            config.timer_interval_ms,
            [&dispatcher, &logger]() {
                const auto purged = dispatcher.PurgeExpired(50);
                if (purged > 0)
                {
                    logger->Info("Active expiration purged " + std::to_string(purged) + " keys");
                }
            },
            config.http_port,
            [&http_service](const auto& req) {
                return http_service.Handle(req);
            });
        server.Run();
    }
    catch (const std::exception& error)
    {
        std::cerr << "gatekeeper: " << error.what() << "\n";
        return 1;
    }

    return 0;
}
