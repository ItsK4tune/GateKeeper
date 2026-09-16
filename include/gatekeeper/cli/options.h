#pragma once

#include "gatekeeper/config/arguments.h"

#include <optional>
#include <string>
#include <string_view>

namespace gatekeeper::cli
{

struct Options
{
    std::string host = "127.0.0.1";
    int port = config::kDefaultPort;
    bool show_help = false;
    std::optional<std::string> command;

    static Options Parse(int argc, const char* const* argv);
    static std::string_view Usage();
};

}
