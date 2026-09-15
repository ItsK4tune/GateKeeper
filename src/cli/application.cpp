#include "gatekeeper/cli/application.h"

#include <istream>
#include <ostream>
#include <string>

namespace gatekeeper::cli
{

Application::Application(const CommandRegistry& commands) : commands_(commands) {}

int Application::RunCommand(std::string_view command, std::ostream& output) const
{
    const auto result = commands_.Execute(command);
    if (!result.output.empty())
    {
        output << result.output << '\n';
    }
    return result.exit_code;
}

int Application::RunRepl(std::istream& input, std::ostream& output) const
{
    std::string command;
    while (output << "gate> " << std::flush, std::getline(input, command))
    {
        const auto result = commands_.Execute(command);
        if (!result.output.empty())
        {
            output << result.output << '\n';
        }
        if (result.exit_requested)
        {
            return result.exit_code;
        }
    }
    return 0;
}

}
