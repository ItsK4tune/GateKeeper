#pragma once

#include "gatekeeper/cli/command.h"

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace gatekeeper::cli
{

class Registry
{
public:
    using Arguments = std::vector<std::string>;
    using Handler = std::function<Result(const Arguments&)>;
    using RemoteExecutor = std::function<std::string(const std::string&)>;
    using RequestExecutor = std::function<std::string(const std::string&, const std::string&)>;

    explicit Registry(RemoteExecutor execute_remote);
    explicit Registry(RequestExecutor execute_remote);
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;
    void Register(std::string name, std::string description, Handler handler);
    Result Execute(std::string_view input) const;

private:
    struct Entry
    {
        std::string description;
        Handler handler;
    };

    std::string BuildHelpOutput() const;

    std::map<std::string, Entry> commands_;
};

using CommandRegistry = Registry;

}
