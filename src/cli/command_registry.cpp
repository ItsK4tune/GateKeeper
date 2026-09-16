#include "gatekeeper/cli/command_registry.h"

#include "gatekeeper/cli/command.h"
#include "gatekeeper/cli/command_suggestions.h"

#include <sstream>
#include <stdexcept>
#include <utility>

namespace gatekeeper::cli
{
namespace
{

CommandRegistry::Handler WithoutArguments(CommandRegistry::Handler handler)
{
    return [handler](const CommandRegistry::Arguments& arguments) {
        if (!arguments.empty())
        {
            return CommandResult{"INVALID_ARGUMENTS: command takes no arguments", false, 1};
        }
        return handler(arguments);
    };
}

}

CommandRegistry::CommandRegistry(RemoteExecutor execute_remote)
{
    Register("PING", "Check server availability", WithoutArguments([execute_remote](const Arguments&) {
        return CommandResult{execute_remote("PING")};
    }));
    Register("HELP", "Show available commands", WithoutArguments([this](const Arguments&) {
        std::string output;
        for (const auto& [name, entry] : commands_)
        {
            output += name + " - " + entry.description + '\n';
        }
        output.pop_back();
        return CommandResult{output};
    }));
    const Handler quit = WithoutArguments([](const Arguments&) { return CommandResult{{}, true}; });
    Register("QUIT", "Close the CLI", quit);
    Register("EXIT", "Alias for QUIT", quit);
}

void CommandRegistry::Register(std::string name, std::string description, Handler handler)
{
    name = NormalizeCommand(std::move(name));
    if (name.empty() || !handler || commands_.contains(name))
    {
        throw std::invalid_argument("invalid or duplicate command registration");
    }
    commands_.emplace(std::move(name), Entry{std::move(description), std::move(handler)});
}

CommandResult CommandRegistry::Execute(std::string_view input) const
{
    std::istringstream stream{std::string(input)};
    std::string name;
    if (!(stream >> name))
    {
        return {};
    }
    name = NormalizeCommand(std::move(name));
    const auto entry = commands_.find(name);
    if (entry == commands_.end())
    {
        std::vector<std::string> names;
        for (const auto& [registered_name, registered_entry] : commands_)
        {
            names.push_back(registered_name);
        }
        const auto suggestions = SuggestCommands(name, names);
        std::string message = "UNKNOWN_COMMAND: unsupported command: " + name;
        if (!suggestions.empty())
        {
            message += "\nDid you mean: ";
            for (std::size_t i = 0; i < suggestions.size(); ++i)
            {
                message += (i == 0 ? "" : ", ") + suggestions[i];
            }
            message += '?';
        }
        message += "\nType HELP to list available commands.";
        return {std::move(message), false, 1};
    }
    Arguments arguments;
    for (std::string argument; stream >> argument;)
    {
        arguments.push_back(std::move(argument));
    }
    try
    {
        return entry->second.handler(arguments);
    }
    catch (const std::exception& error)
    {
        return {error.what(), false, 1};
    }
}

}
