#pragma once

#include "gatekeeper/core/config/arguments.h"
#include "gatekeeper/core/log/logger.h"

#include <optional>
#include <string>
#include <string_view>

namespace gatekeeper::config
{

struct CliConfig
{
    std::string host = "127.0.0.1";
    int port = kDefaultPort;
    bool show_help = false;
    log::Mode log_mode = log::Mode::None;
    std::optional<std::string> command;

    static CliConfig Parse(int argc, const char* const* argv);
    static std::string_view Usage();
};

}
