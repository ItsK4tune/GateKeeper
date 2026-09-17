#include "gatekeeper/config/cli_config.h"
#include "gatekeeper/config/server_config.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

void Require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

template <typename Options>
Options Parse(std::initializer_list<const char*> arguments)
{
    const std::vector<const char*> values(arguments);
    return Options::Parse(static_cast<int>(values.size()), values.data());
}

template <typename Options>
void TestCommonRules()
{
    Require(Parse<Options>({"app"}).port == 63779, "default port changed");
    Require(Parse<Options>({"app", "-p", "1"}).port == 1, "minimum port rejected");
    Require(Parse<Options>({"app", "--port", "65535"}).port == 65535, "maximum port rejected");
    Require(Parse<Options>({"app", "--help"}).show_help, "help flag ignored");
    for (const auto* value : {"0", "-1", "65536", "abc", "12x", "", "99999999999999"})
    {
        bool failed = false;
        try
        {
            Parse<Options>({"app", "-p", value});
        }
        catch (const std::invalid_argument& error)
        {
            failed = std::string(error.what()).starts_with("INVALID_PORT:");
        }
        Require(failed, "invalid port accepted");
    }
    for (const auto arguments : {std::initializer_list<const char*>{"app", "-p"},
                                {"app", "-p", "64000", "--port", "64001"},
                                {"app", "--unknown"}})
    {
        bool failed = false;
        try
        {
            Parse<Options>(arguments);
        }
        catch (const std::invalid_argument& error)
        {
            failed = std::string(error.what()).starts_with("INVALID_OPTION:");
        }
        Require(failed, "invalid option accepted");
    }
}

}

int main()
{
    try
    {
        using Cli = gatekeeper::config::CliConfig;
        using Server = gatekeeper::config::ServerConfig;
        TestCommonRules<Cli>();
        TestCommonRules<Server>();
        const auto defaults = Parse<Cli>({"gate"});
        Require(defaults.host == "127.0.0.1" && !defaults.command, "CLI defaults changed");
        const auto cli = Parse<Cli>({"gate", "--host", "localhost", "--port", "64000", "echo", "DuOnG", "-p"});
        Require(cli.host == "localhost" && cli.port == 64000, "long options failed");
        Require(cli.command == "echo DuOnG -p", "command arguments were changed or parsed as options");
        Require(Parse<Cli>({"gate", "-h", "localhost", "PING"}).host == "localhost", "CLI -h changed");
        Require(Parse<Server>({"gatekeeper", "-h"}).show_help, "service -h changed");
        bool failed = false;
        try
        {
            Parse<Cli>({"gate", "-h", ""});
        }
        catch (const std::invalid_argument& error)
        {
            failed = std::string(error.what()).starts_with("INVALID_HOST:");
        }
        Require(failed, "empty host accepted");
        std::cout << "Options tests passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
