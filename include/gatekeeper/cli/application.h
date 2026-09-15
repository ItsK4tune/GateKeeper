#pragma once

#include "gatekeeper/cli/command_registry.h"

#include <iosfwd>
#include <string_view>

namespace gatekeeper::cli
{

class Application
{
public:
    explicit Application(const CommandRegistry& commands);
    int RunCommand(std::string_view command, std::ostream& output) const;
    int RunRepl(std::istream& input, std::ostream& output) const;

private:
    const CommandRegistry& commands_;
};

}
