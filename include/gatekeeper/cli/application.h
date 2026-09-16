#pragma once

#include "gatekeeper/cli/command_registry.h"

#include <functional>
#include <iosfwd>
#include <string_view>

namespace gatekeeper::cli
{

class Application
{
public:
    using SessionStarter = std::function<void()>;

    Application(const CommandRegistry& commands, SessionStarter session_starter);
    int RunCommand(std::string_view command, std::ostream& output) const;
    int RunRepl(std::istream& input, std::ostream& output) const;

private:
    const CommandRegistry& commands_;
    SessionStarter session_starter_;
};

}
