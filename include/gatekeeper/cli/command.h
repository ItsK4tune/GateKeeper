#pragma once

#include <algorithm>
#include <cctype>
#include <string>

namespace gatekeeper::cli
{

inline std::string NormalizeCommand(std::string command)
{
    std::transform(command.begin(), command.end(), command.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    return command;
}

}
