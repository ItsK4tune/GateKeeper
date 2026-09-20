#include "gatekeeper/core/config/server_config.h"
#include "gatekeeper/command/dispatcher.h"
#include "gatekeeper/core/log/logger.h"
#include "gatekeeper/net/server.h"
#include "gatekeeper/service/processor.h"
#include "gatekeeper/service/http_service.h"
#include "gatekeeper/storage/aof/aof_writer.h"
#include "gatekeeper/storage/aof/aof_loader.h"

#include <atomic>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <string>

namespace
{
std::atomic<gatekeeper::net::Server*> g_server{nullptr};

extern "C" void HandleSignal(int /*signum*/)
{
    if (auto* s = g_server.load())
    {
        s->Stop();
    }
}
}

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
        std::shared_ptr<gatekeeper::storage::aof::AofWriter> aof_writer;
        if (config.persistence == "aof")
        {
            std::filesystem::create_directories(config.data_dir);
            const auto aof_path = config.data_dir + "/gatekeeper.aof";
            const auto load_res = gatekeeper::storage::aof::AofLoader::Load(aof_path, dispatcher);
            if (load_res.ok && load_res.lines_replayed > 0)
            {
                logger->Info("AOF loaded " + std::to_string(load_res.lines_replayed) + " commands from " + aof_path);
            }
            auto policy = gatekeeper::storage::aof::FsyncPolicy::Everysec;
            if (config.fsync == "always")
            {
                policy = gatekeeper::storage::aof::FsyncPolicy::Always;
            }
            else if (config.fsync == "no")
            {
                policy = gatekeeper::storage::aof::FsyncPolicy::No;
            }

            aof_writer = std::make_shared<gatekeeper::storage::aof::AofWriter>(aof_path, policy);
            dispatcher.SetAofWriter(aof_writer);
        }

        gatekeeper::service::Processor processor(&dispatcher.GetStore(), logger, aof_writer);
        gatekeeper::service::HttpService http_service(dispatcher.GetStore(), logger, aof_writer);

        gatekeeper::net::Server server(
            config.port,
            [&processor](std::string_view payload) {
                return processor.Process(payload);
            },
            logger,
            config.timer_interval_ms,
            [&dispatcher, logger, aof_writer]() {
                if (aof_writer)
                {
                    aof_writer->Fsync();
                }
                const auto purged = dispatcher.PurgeExpired(50);
                if (purged > 0)
                {
                    logger->Info("Active expiration purged " + std::to_string(purged) + " keys");
                }
            },
            config.http_port,
            [&http_service](const auto& req) {
                return http_service.Handle(req);
            },
            config.workers);

        server.SetDisconnectCallback([&dispatcher, logger](std::uint64_t session_id) {
            auto released = dispatcher.GetStore().ReleaseSessionLocks(session_id);
            if (!released.empty())
            {
                logger->Info("Auto-released " + std::to_string(released.size()) + " ephemeral locks for disconnected session " + std::to_string(session_id));
            }
        });

        g_server.store(&server);
        struct sigaction sa{};
        sa.sa_handler = HandleSignal;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGINT, &sa, nullptr);
        sigaction(SIGTERM, &sa, nullptr);

        server.Run();

        g_server.store(nullptr);
        if (aof_writer)
        {
            aof_writer->Close();
        }
        logger->Info("GateKeeper server gracefully stopped.");
    }
    catch (const std::exception& error)
    {
        std::cerr << "gatekeeper: " << error.what() << "\n";
        return 1;
    }

    return 0;
}
