#pragma once

#include "gatekeeper/command/name.h"
#include <string>

namespace gatekeeper::cli
{

struct Result
{
    std::string output;
    bool exit_requested = false;
    int exit_code = 0;
};

inline std::string NormalizeCommand(std::string command)
{
    return gatekeeper::command::NormalizeName(command);
}

}
