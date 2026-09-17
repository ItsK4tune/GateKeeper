#pragma once

#include "gatekeeper/config/arguments.h"
#include "gatekeeper/log/logger.h"

#include <string>
#include <string_view>

namespace gatekeeper::config
{

struct ServerConfig
{
    int port = kDefaultPort;
    bool show_help = false;
    log::Mode log_mode = log::Mode::None;
    std::string log_dir;
    int timer_interval_ms = -1;

    static ServerConfig Parse(int argc, const char* const* argv);
    static std::string_view Usage();
};

}

namespace gatekeeper::service
{
using Options = config::ServerConfig;
}
