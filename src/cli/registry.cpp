#include "gatekeeper/cli/registry.h"
#include "gatekeeper/cli/format.h"
#include "gatekeeper/cli/command.h"
#include "gatekeeper/cli/suggest.h"
#include "gatekeeper/protocol/response.h"

#include <iomanip>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace gatekeeper::cli
{
namespace
{

Registry::Handler WithoutArguments(Registry::Handler handler)
{
    return [handler](const Registry::Arguments& arguments) {
        if (!arguments.empty())
        {
            return Result{"INVALID_ARGUMENTS: command takes no arguments", false, 1};
        }
        return handler(arguments);
    };
}

}

Registry::Registry(RemoteExecutor execute_remote)
    : Registry(RequestExecutor{[execute_remote](const std::string& op, const std::string& body) {
        if (body != "{}") throw std::invalid_argument("transport does not support command arguments");
        return execute_remote(op);
    }})
{
}

Registry::Registry(RequestExecutor execute_remote)
{
    Register("PING", "Check server availability", WithoutArguments([execute_remote](const Arguments&) {
        return FormatResponse(execute_remote("PING", "{}"));
    }));
    Register("GET", "GET key", [execute_remote](const Arguments& args) {
        if (args.size() != 1 || args[0].empty())
            return Result{"INVALID_ARGUMENTS: GET key", false, 1};
        return FormatResponse(execute_remote("GET", "{\"key\":" + protocol::QuoteJson(args[0]) + "}"));
    });
    Register("SET", "SET key value [NX|XX]", [execute_remote](const Arguments& args) {
        if (args.size() < 2 || args.size() > 3 || args[0].empty())
            return Result{"INVALID_ARGUMENTS: SET key value [NX|XX]", false, 1};
        std::string body = "{\"key\":" + protocol::QuoteJson(args[0]) +
                           ",\"value\":" + protocol::QuoteJson(args[1]);
        if (args.size() == 3) {
            const auto flag = NormalizeCommand(args[2]);
            if (flag == "NX") body += ",\"if_not_exists\":true";
            else if (flag == "XX") body += ",\"if_exists\":true";
            else return Result{"INVALID_ARGUMENTS: expected NX or XX", false, 1};
        }
        return FormatResponse(execute_remote("SET", body + "}"));
    });
    Register("HELP", "Show available commands", WithoutArguments([this](const Arguments&) {
        std::string output;
        for (const auto& [name, entry] : commands_)
        {
            output += name + " - " + entry.description + '\n';
        }
        output.pop_back();
        return Result{output};
    }));
    const Handler quit = WithoutArguments([](const Arguments&) { return Result{{}, true}; });
    Register("QUIT", "Close the CLI", quit);
    Register("EXIT", "Alias for QUIT", quit);
}

void Registry::Register(std::string name, std::string description, Handler handler)
{
    name = NormalizeCommand(std::move(name));
    if (name.empty() || !handler || commands_.contains(name))
    {
        throw std::invalid_argument("invalid or duplicate command registration");
    }
    commands_.emplace(std::move(name), Entry{std::move(description), std::move(handler)});
}

Result Registry::Execute(std::string_view input) const
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
        const auto suggestions = Suggest(name, names);
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
    while (stream >> std::ws && !stream.eof()) {
        std::string argument;
        if (!(stream >> std::quoted(argument)))
            return {"INVALID_ARGUMENTS: unterminated quoted argument", false, 1};
        if (stream.peek() != std::char_traits<char>::eof() &&
            !std::isspace(static_cast<unsigned char>(stream.peek())))
            return {"INVALID_ARGUMENTS: expected whitespace after argument", false, 1};
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
