#pragma once

#include "gatekeeper/core/config/arguments.h"
#include "gatekeeper/core/log/logger.h"

#include <string>
#include <string_view>

namespace gatekeeper::config
{

struct ServerConfig
{
    int port = kDefaultPort;
    int http_port = 0;
    bool show_help = false;
    log::Mode log_mode = log::Mode::None;
    std::string log_dir;
    int timer_interval_ms = -1;
    std::string persistence = "none";
    std::string data_dir = "./data";
    std::string fsync = "everysec";
    std::size_t workers = 0; // 0 = all cores

    static ServerConfig Parse(int argc, const char* const* argv);
    static std::string_view Usage();
};

}
