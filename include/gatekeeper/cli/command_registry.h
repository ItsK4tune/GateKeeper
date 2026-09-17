#pragma once

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace gatekeeper::cli
{

struct CommandResult
{
    std::string output;
    bool exit_requested = false;
    int exit_code = 0;
};

class CommandRegistry
{
public:
    using Arguments = std::vector<std::string>;
    using Handler = std::function<CommandResult(const Arguments&)>;
    using RemoteExecutor = std::function<std::string(const std::string&)>;
    using RequestExecutor = std::function<std::string(const std::string&, const std::string&)>;

    explicit CommandRegistry(RemoteExecutor execute_remote);
    explicit CommandRegistry(RequestExecutor execute_remote);
    CommandRegistry(const CommandRegistry&) = delete;
    CommandRegistry& operator=(const CommandRegistry&) = delete;
    void Register(std::string name, std::string description, Handler handler);
    CommandResult Execute(std::string_view input) const;

private:
    struct Entry
    {
        std::string description;
        Handler handler;
    };

    std::map<std::string, Entry> commands_;
};

}
