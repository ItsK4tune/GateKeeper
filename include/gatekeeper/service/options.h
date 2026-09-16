#pragma once

#include "gatekeeper/config/arguments.h"

#include <string_view>

namespace gatekeeper::service
{

struct Options
{
    int port = config::kDefaultPort;
    bool show_help = false;

    static Options Parse(int argc, const char* const* argv);
    static std::string_view Usage();
};

}
