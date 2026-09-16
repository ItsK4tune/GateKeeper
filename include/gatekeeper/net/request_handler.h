#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace gatekeeper::net
{

using RequestHandler = std::function<std::string(std::string_view)>;

}
