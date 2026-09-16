#pragma once

#include <string>
#include <string_view>

namespace gatekeeper::command
{

std::string NormalizeName(std::string_view name);

}
