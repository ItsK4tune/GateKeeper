#include "gatekeeper/command/command_dispatcher.h"

#include <algorithm>
#include <cctype>

namespace gatekeeper::command
{

DispatchResult CommandDispatcher::Dispatch(const protocol::Request& request) const
{
    std::string operation = request.op;
    std::transform(operation.begin(), operation.end(), operation.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });

    if (operation == "PING")
    {
        return {true, R"({"pong":true})", {}};
    }

    return {false, {}, {"UNKNOWN_COMMAND", "unsupported command: " + request.op}};
}

}
