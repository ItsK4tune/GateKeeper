#pragma once

#include "gatekeeper/cli/command.h"

#include <string_view>

namespace gatekeeper::cli
{

Result FormatResponse(std::string_view gkwp_json);

}

