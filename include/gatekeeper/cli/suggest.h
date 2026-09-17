#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace gatekeeper::cli
{

std::vector<std::string> Suggest(std::string_view input, const std::vector<std::string>& names);

inline std::vector<std::string> SuggestCommands(std::string_view input, const std::vector<std::string>& names)
{
    return Suggest(input, names);
}

}
