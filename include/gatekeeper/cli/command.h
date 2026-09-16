#pragma once

#include "gatekeeper/command/name.h"
#include <string>

namespace gatekeeper::cli
{

inline std::string NormalizeCommand(std::string command)
{
    return gatekeeper::command::NormalizeName(command);
}

}
