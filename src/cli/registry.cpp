#include "gatekeeper/cli/registry.h"
#include "gatekeeper/cli/format.h"
#include "gatekeeper/cli/suggest.h"
#include "gatekeeper/command/name.h"
#include "gatekeeper/protocol/response.h"

#include <cctype>
#include <iomanip>
#include <set>
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

std::string Registry::BuildHelpOutput() const
{
    struct Group
    {
        std::string title;
        std::vector<std::pair<std::string, std::string>> items;
    };

    std::vector<Group> groups = {
        {"Server & Session", {
            {"PING", "Check server availability"},
            {"HELP", "Show available commands"},
            {"QUIT", "Close the CLI"},
            {"EXIT", "Alias for QUIT"}
        }},
        {"String Operations", {
            {"GET", "GET key"},
            {"SET", "SET key value [NX|XX] [EX seconds|PX milliseconds]"}
        }},
        {"Rate Limiting & Counters", {
            {"INCR", "INCR key"},
            {"DECR", "DECR key"},
            {"INCRBY", "INCRBY key delta [EX seconds|PX milliseconds]"},
            {"GK.RATE_LIMIT", "GK.RATE_LIMIT key limit window_ms"}
        }},
        {"Two-Phase Quota Reservation", {
            {"GK.RESERVE", "GK.RESERVE key amount ttl_ms"},
            {"GK.COMMIT", "GK.COMMIT key reservation_id actual_amount"},
            {"GK.ROLLBACK", "GK.ROLLBACK key reservation_id"},
            {"GK.QUOTA_INIT", "GK.QUOTA_INIT key amount"},
            {"GK.QUOTA_GET", "GK.QUOTA_GET key"}
        }},
        {"Idempotency & Single-Flight", {
            {"GK.IDEM_BEGIN", "GK.IDEM_BEGIN key request_hash [ttl_ms] [owner_token]"},
            {"GK.IDEM_COMPLETE", "GK.IDEM_COMPLETE key owner_token response_code [response_body]"},
            {"GK.IDEM_FAIL", "GK.IDEM_FAIL key owner_token [error_message]"},
            {"GK.IDEM_GET", "GK.IDEM_GET key"}
        }},
        {"Key Management", {
            {"DEL", "DEL key [key ...]"},
            {"EXISTS", "EXISTS key [key ...]"},
            {"TYPE", "TYPE key"},
            {"DBSIZE", "Return total count of keys in database"},
            {"KEYS", "KEYS [pattern]"},
            {"SCAN", "SCAN [cursor] [count]"}
        }},
        {"Expiration & TTL", {
            {"EXPIRE", "EXPIRE key seconds"},
            {"PEXPIRE", "PEXPIRE key milliseconds"},
            {"TTL", "TTL key"},
            {"PTTL", "PTTL key"},
            {"PERSIST", "PERSIST key"}
        }}
    };

    std::set<std::string> recognized;
    for (const auto& g : groups)
    {
        for (const auto& [name, _] : g.items)
        {
            recognized.insert(name);
        }
    }

    std::vector<std::pair<std::string, std::string>> custom_items;
    for (const auto& [name, entry] : commands_)
    {
        if (!recognized.contains(name))
        {
            custom_items.push_back({name, entry.description});
        }
    }
    if (!custom_items.empty())
    {
        groups.push_back({"Extensions", std::move(custom_items)});
    }

    std::ostringstream out;
    for (std::size_t g = 0; g < groups.size(); ++g)
    {
        if (g > 0)
        {
            out << '\n';
        }
        out << "[" << groups[g].title << "]\n";
        for (const auto& [name, desc] : groups[g].items)
        {
            out << "  " << name << " - " << desc << '\n';
        }
    }

    std::string res = out.str();
    if (!res.empty() && res.back() == '\n')
    {
        res.pop_back();
    }
    return res;
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
    Register("SET", "SET key value [NX|XX] [EX seconds|PX milliseconds]", [execute_remote](const Arguments& args) {
        if (args.size() < 2 || args[0].empty())
            return Result{"INVALID_ARGUMENTS: SET key value [NX|XX] [EX seconds|PX milliseconds]", false, 1};
        std::string body = "{\"key\":" + protocol::QuoteJson(args[0]) +
                           ",\"value\":" + protocol::QuoteJson(args[1]);
        bool has_cond = false;
        bool has_ttl = false;
        for (std::size_t i = 2; i < args.size(); ++i) {
            const auto flag = NormalizeCommand(args[i]);
            if ((flag == "NX" || flag == "XX") && !has_cond) {
                if (flag == "NX") body += ",\"if_not_exists\":true";
                else body += ",\"if_exists\":true";
                has_cond = true;
            } else if (flag == "EX" && i + 1 < args.size() && !has_ttl) {
                try {
                    const auto sec = std::stoull(args[++i]);
                    body += ",\"ttl_ms\":" + std::to_string(sec * 1000);
                    has_ttl = true;
                } catch (...) {
                    return Result{"INVALID_ARGUMENTS: invalid EX value", false, 1};
                }
            } else if (flag == "PX" && i + 1 < args.size() && !has_ttl) {
                try {
                    const auto ms = std::stoull(args[++i]);
                    body += ",\"ttl_ms\":" + std::to_string(ms);
                    has_ttl = true;
                } catch (...) {
                    return Result{"INVALID_ARGUMENTS: invalid PX value", false, 1};
                }
            } else {
                return Result{"INVALID_ARGUMENTS: syntax error in SET", false, 1};
            }
        }
        return FormatResponse(execute_remote("SET", body + "}"));
    });
    Register("INCR", "INCR key", [execute_remote](const Arguments& args) {
        if (args.size() != 1 || args[0].empty())
            return Result{"INVALID_ARGUMENTS: INCR key", false, 1};
        return FormatResponse(execute_remote("INCR", "{\"key\":" + protocol::QuoteJson(args[0]) + "}"));
    });
    Register("DECR", "DECR key", [execute_remote](const Arguments& args) {
        if (args.size() != 1 || args[0].empty())
            return Result{"INVALID_ARGUMENTS: DECR key", false, 1};
        return FormatResponse(execute_remote("DECR", "{\"key\":" + protocol::QuoteJson(args[0]) + "}"));
    });
    Register("INCRBY", "INCRBY key delta [EX seconds|PX milliseconds]", [execute_remote](const Arguments& args) {
        if (args.size() < 2 || args[0].empty() || args[1].empty())
            return Result{"INVALID_ARGUMENTS: INCRBY key delta [EX seconds|PX milliseconds]", false, 1};
        std::string body = "{\"key\":" + protocol::QuoteJson(args[0]) + ",\"delta\":" + args[1];
        if (args.size() >= 4) {
            auto flag = NormalizeCommand(args[2]);
            if (flag == "EX") body += ",\"ttl_seconds\":" + args[3];
            else if (flag == "PX") body += ",\"ttl_ms\":" + args[3];
        }
        body += "}";
        return FormatResponse(execute_remote("INCRBY", body));
    });
    Register("GK.RATE_LIMIT", "GK.RATE_LIMIT key limit window_ms", [execute_remote](const Arguments& args) {
        if (args.size() != 3 || args[0].empty() || args[1].empty() || args[2].empty())
            return Result{"INVALID_ARGUMENTS: GK.RATE_LIMIT key limit window_ms", false, 1};
        return FormatResponse(execute_remote("GK.RATE_LIMIT", "{\"key\":" + protocol::QuoteJson(args[0]) +
            ",\"limit\":" + args[1] + ",\"window_ms\":" + args[2] + "}"));
    });
    Register("GK.RESERVE", "GK.RESERVE key amount ttl_ms", [execute_remote](const Arguments& args) {
        if (args.size() != 3 || args[0].empty() || args[1].empty() || args[2].empty())
            return Result{"INVALID_ARGUMENTS: GK.RESERVE key amount ttl_ms", false, 1};
        return FormatResponse(execute_remote("GK.RESERVE", "{\"key\":" + protocol::QuoteJson(args[0]) +
            ",\"amount\":" + args[1] + ",\"ttl_ms\":" + args[2] + "}"));
    });
    Register("GK.COMMIT", "GK.COMMIT key reservation_id actual_amount", [execute_remote](const Arguments& args) {
        if (args.size() != 3 || args[0].empty() || args[1].empty() || args[2].empty())
            return Result{"INVALID_ARGUMENTS: GK.COMMIT key reservation_id actual_amount", false, 1};
        return FormatResponse(execute_remote("GK.COMMIT", "{\"key\":" + protocol::QuoteJson(args[0]) +
            ",\"reservation_id\":" + protocol::QuoteJson(args[1]) + ",\"actual_amount\":" + args[2] + "}"));
    });
    Register("GK.ROLLBACK", "GK.ROLLBACK key reservation_id", [execute_remote](const Arguments& args) {
        if (args.size() != 2 || args[0].empty() || args[1].empty())
            return Result{"INVALID_ARGUMENTS: GK.ROLLBACK key reservation_id", false, 1};
        return FormatResponse(execute_remote("GK.ROLLBACK", "{\"key\":" + protocol::QuoteJson(args[0]) +
            ",\"reservation_id\":" + protocol::QuoteJson(args[1]) + "}"));
    });
    Register("GK.QUOTA_INIT", "GK.QUOTA_INIT key amount", [execute_remote](const Arguments& args) {
        if (args.size() != 2 || args[0].empty() || args[1].empty())
            return Result{"INVALID_ARGUMENTS: GK.QUOTA_INIT key amount", false, 1};
        return FormatResponse(execute_remote("GK.QUOTA_INIT", "{\"key\":" + protocol::QuoteJson(args[0]) +
            ",\"amount\":" + args[1] + "}"));
    });
    Register("GK.QUOTA_GET", "GK.QUOTA_GET key", [execute_remote](const Arguments& args) {
        if (args.size() != 1 || args[0].empty())
            return Result{"INVALID_ARGUMENTS: GK.QUOTA_GET key", false, 1};
        return FormatResponse(execute_remote("GK.QUOTA_GET", "{\"key\":" + protocol::QuoteJson(args[0]) + "}"));
    });
    Register("GK.IDEM_BEGIN", "GK.IDEM_BEGIN key request_hash [ttl_ms] [owner_token]", [execute_remote](const Arguments& args) {
        if (args.size() < 2 || args[0].empty() || args[1].empty())
            return Result{"INVALID_ARGUMENTS: GK.IDEM_BEGIN key request_hash [ttl_ms] [owner_token]", false, 1};
        std::string body = "{\"key\":" + protocol::QuoteJson(args[0]) + ",\"request_hash\":" + protocol::QuoteJson(args[1]);
        if (args.size() >= 3 && !args[2].empty()) body += ",\"ttl_ms\":" + args[2];
        if (args.size() >= 4 && !args[3].empty()) body += ",\"owner_token\":" + protocol::QuoteJson(args[3]);
        body += "}";
        return FormatResponse(execute_remote("GK.IDEM_BEGIN", body));
    });
    Register("GK.IDEM_COMPLETE", "GK.IDEM_COMPLETE key owner_token response_code [response_body]", [execute_remote](const Arguments& args) {
        if (args.size() < 3 || args[0].empty() || args[1].empty() || args[2].empty())
            return Result{"INVALID_ARGUMENTS: GK.IDEM_COMPLETE key owner_token response_code [response_body]", false, 1};
        std::string body = "{\"key\":" + protocol::QuoteJson(args[0]) + ",\"owner_token\":" + protocol::QuoteJson(args[1]) + ",\"response_code\":" + args[2];
        if (args.size() >= 4) body += ",\"response_body\":" + protocol::QuoteJson(args[3]);
        body += "}";
        return FormatResponse(execute_remote("GK.IDEM_COMPLETE", body));
    });
    Register("GK.IDEM_FAIL", "GK.IDEM_FAIL key owner_token [error_message]", [execute_remote](const Arguments& args) {
        if (args.size() < 2 || args[0].empty() || args[1].empty())
            return Result{"INVALID_ARGUMENTS: GK.IDEM_FAIL key owner_token [error_message]", false, 1};
        std::string body = "{\"key\":" + protocol::QuoteJson(args[0]) + ",\"owner_token\":" + protocol::QuoteJson(args[1]);
        if (args.size() >= 3) body += ",\"error_message\":" + protocol::QuoteJson(args[2]);
        body += "}";
        return FormatResponse(execute_remote("GK.IDEM_FAIL", body));
    });
    Register("GK.IDEM_GET", "GK.IDEM_GET key", [execute_remote](const Arguments& args) {
        if (args.size() != 1 || args[0].empty())
            return Result{"INVALID_ARGUMENTS: GK.IDEM_GET key", false, 1};
        return FormatResponse(execute_remote("GK.IDEM_GET", "{\"key\":" + protocol::QuoteJson(args[0]) + "}"));
    });
    Register("DEL", "DEL key [key ...]", [execute_remote](const Arguments& args) {
        if (args.empty()) return Result{"INVALID_ARGUMENTS: DEL key [key ...]", false, 1};
        std::string body = "{\"keys\":[";
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (i > 0) body += ",";
            body += protocol::QuoteJson(args[i]);
        }
        body += "]}";
        return FormatResponse(execute_remote("DEL", body));
    });
    Register("EXISTS", "EXISTS key [key ...]", [execute_remote](const Arguments& args) {
        if (args.empty()) return Result{"INVALID_ARGUMENTS: EXISTS key [key ...]", false, 1};
        std::string body = "{\"keys\":[";
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (i > 0) body += ",";
            body += protocol::QuoteJson(args[i]);
        }
        body += "]}";
        return FormatResponse(execute_remote("EXISTS", body));
    });
    Register("TYPE", "TYPE key", [execute_remote](const Arguments& args) {
        if (args.size() != 1 || args[0].empty()) return Result{"INVALID_ARGUMENTS: TYPE key", false, 1};
        return FormatResponse(execute_remote("TYPE", "{\"key\":" + protocol::QuoteJson(args[0]) + "}"));
    });
    Register("DBSIZE", "DBSIZE", WithoutArguments([execute_remote](const Arguments&) {
        return FormatResponse(execute_remote("DBSIZE", "{}"));
    }));
    Register("KEYS", "KEYS [pattern]", [execute_remote](const Arguments& args) {
        if (args.size() > 1) return Result{"INVALID_ARGUMENTS: KEYS [pattern]", false, 1};
        std::string pattern = args.empty() ? "*" : args[0];
        return FormatResponse(execute_remote("KEYS", "{\"pattern\":" + protocol::QuoteJson(pattern) + "}"));
    });
    Register("SCAN", "SCAN [cursor] [count]", [execute_remote](const Arguments& args) {
        if (args.size() > 2) return Result{"INVALID_ARGUMENTS: SCAN [cursor] [count]", false, 1};
        std::string cursor = args.empty() ? "0" : args[0];
        std::string count = args.size() > 1 ? args[1] : "10";
        return FormatResponse(execute_remote("SCAN", "{\"cursor\":" + cursor + ",\"count\":" + count + "}"));
    });
    Register("EXPIRE", "EXPIRE key seconds", [execute_remote](const Arguments& args) {
        if (args.size() != 2 || args[0].empty() || args[1].empty())
            return Result{"INVALID_ARGUMENTS: EXPIRE key seconds", false, 1};
        return FormatResponse(execute_remote("EXPIRE", "{\"key\":" + protocol::QuoteJson(args[0]) + ",\"ttl_seconds\":" + args[1] + "}"));
    });
    Register("PEXPIRE", "PEXPIRE key milliseconds", [execute_remote](const Arguments& args) {
        if (args.size() != 2 || args[0].empty() || args[1].empty())
            return Result{"INVALID_ARGUMENTS: PEXPIRE key milliseconds", false, 1};
        return FormatResponse(execute_remote("PEXPIRE", "{\"key\":" + protocol::QuoteJson(args[0]) + ",\"ttl_ms\":" + args[1] + "}"));
    });
    Register("TTL", "TTL key", [execute_remote](const Arguments& args) {
        if (args.size() != 1 || args[0].empty())
            return Result{"INVALID_ARGUMENTS: TTL key", false, 1};
        return FormatResponse(execute_remote("TTL", "{\"key\":" + protocol::QuoteJson(args[0]) + "}"));
    });
    Register("PTTL", "PTTL key", [execute_remote](const Arguments& args) {
        if (args.size() != 1 || args[0].empty())
            return Result{"INVALID_ARGUMENTS: PTTL key", false, 1};
        return FormatResponse(execute_remote("PTTL", "{\"key\":" + protocol::QuoteJson(args[0]) + "}"));
    });
    Register("PERSIST", "PERSIST key", [execute_remote](const Arguments& args) {
        if (args.size() != 1 || args[0].empty())
            return Result{"INVALID_ARGUMENTS: PERSIST key", false, 1};
        return FormatResponse(execute_remote("PERSIST", "{\"key\":" + protocol::QuoteJson(args[0]) + "}"));
    });
    Register("HELP", "Show available commands", WithoutArguments([this](const Arguments&) {
        return Result{BuildHelpOutput()};
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
