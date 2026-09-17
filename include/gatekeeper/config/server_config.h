#pragma once

#include "gatekeeper/config/arguments.h"

#include <string_view>

namespace gatekeeper::config
{

struct ServerConfig
{
    int port = kDefaultPort;
    bool show_help = false;

    static ServerConfig Parse(int argc, const char* const* argv);
    static std::string_view Usage();
};

}

namespace gatekeeper::service
{
using Options = config::ServerConfig;
}
