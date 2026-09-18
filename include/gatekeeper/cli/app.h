#pragma once

#include "gatekeeper/cli/registry.h"

#include <functional>
#include <iosfwd>
#include <string_view>

namespace gatekeeper::cli
{

class App
{
public:
    using SessionStarter = std::function<void()>;

    explicit App(const Registry& commands, SessionStarter session_starter = {});

    int RunCommand(std::string_view command, std::ostream& output) const;
    int RunRepl(std::istream& input, std::ostream& output) const;

private:
    const Registry& commands_;
    SessionStarter session_starter_;
};

}
