#include "gatekeeper/cli/application.h"

#include <iostream>
#include <sstream>
#include <stdexcept>
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

std::vector<std::string> Variants(std::string word)
{
    std::vector<std::string> variants;
    for (unsigned mask = 0; mask < (1U << word.size()); ++mask)
    {
        auto variant = word;
        for (std::size_t index = 0; index < word.size(); ++index)
        {
            if (mask & (1U << index))
            {
                variant[index] += 'a' - 'A';
            }
        }
        variants.push_back(variant);
    }
    return variants;
}

void TestCommandsAndExtension()
{
    std::vector<std::string> requests;
    gatekeeper::cli::CommandRegistry registry([&](const std::string& operation) {
        requests.push_back(operation);
        return std::string("PONG");
    });
    for (const auto& variant : Variants("HELP"))
    {
        const auto result = registry.Execute(" \t" + variant + " \t");
        Require(result.output.find("PING") != std::string::npos && result.exit_code == 0, "help failed");
    }
    for (const auto& word : {"QUIT", "EXIT"})
    {
        for (const auto& variant : Variants(word))
        {
            Require(registry.Execute(variant).exit_requested, "exit variant failed");
        }
    }
    Require(requests.empty(), "local commands sent network requests");
    for (const auto& variant : Variants("PING"))
    {
        Require(registry.Execute(variant).output == "PONG", "ping variant failed");
        Require(requests.back() == "PING", "remote name was not normalized");
    }
    const auto count = requests.size();
    Require(registry.Execute("PING ignored").exit_code != 0, "arguments were silently dropped");
    Require(!registry.Execute("quit extra").exit_requested, "invalid quit exited");
    Require(registry.Execute("missing").exit_code != 0, "unknown command accepted");
    Require(requests.size() == count, "invalid command reached transport");
    registry.Register("Echo", "Return arguments", [](const auto& arguments) {
        return gatekeeper::cli::CommandResult{arguments.at(0)};
    });
    Require(registry.Execute("eChO DuOnG").output == "DuOnG", "argument case changed");
    Require(registry.Execute("help").output.find("ECHO - Return arguments") != std::string::npos,
            "new command missing from help");
}

void TestApplicationModes()
{
    int requests = 0;
    gatekeeper::cli::CommandRegistry registry([&](const std::string&) {
        ++requests;
        return std::string("PONG");
    });
    gatekeeper::cli::Application application(registry);
    for (const auto& word : {"QUIT", "EXIT"})
    {
        for (const auto& variant : Variants(word))
        {
            std::istringstream input("hElP\npInG\n" + variant + "\nPING\n");
            std::ostringstream output;
            const auto previous = requests;
            Require(application.RunRepl(input, output) == 0, "REPL failed");
            Require(requests == previous + 1, "REPL sent quit or read after exit");
            output.str("");
            Require(application.RunCommand(variant, output) == 0, "one-line exit failed");
            Require(output.str().empty() && requests == previous + 1, "one-line exit sent request");
        }
    }
    std::istringstream input(" \t\n");
    std::ostringstream output;
    const auto previous = requests;
    Require(application.RunRepl(input, output) == 0 && requests == previous, "blank input or EOF sent request");
    Require(application.RunCommand("pInG", output) == 0 && requests == previous + 1, "one-line ping failed");
}

void TestSuggestions()
{
    int requests = 0;
    gatekeeper::cli::CommandRegistry registry([&](const std::string&) {
        ++requests;
        return std::string("PONG");
    });
    for (const auto& typo : {"pin", "pniG", "pingg", "pimg"})
    {
        const auto result = registry.Execute(typo);
        Require(result.exit_code != 0 && !result.exit_requested, "typo executed");
        Require(result.output.find("Did you mean: PING?") != std::string::npos, "missing PING suggestion");
    }
    Require(registry.Execute("hlep").output.find("Did you mean: HELP?") != std::string::npos, "missing HELP suggestion");
    Require(registry.Execute("xxxxxxxxxxxxxxxx").output.find("Did you mean") == std::string::npos, "unrelated suggestion");
    Require(registry.Execute(std::string(10000, 'x')).exit_code != 0, "long unknown operation accepted");
    registry.Register("PONG", "Test extension", [](const auto&) { return gatekeeper::cli::CommandResult{}; });
    registry.Register("PANG", "Test extension", [](const auto&) { return gatekeeper::cli::CommandResult{}; });
    registry.Register("PUNG", "Test extension", [](const auto&) { return gatekeeper::cli::CommandResult{}; });
    const auto result = registry.Execute("peng");
    Require(result.output.find("Did you mean: PANG, PING, PONG?") != std::string::npos, "suggestion ranking or limit failed");
    Require(requests == 0, "suggestions executed remote commands");
    gatekeeper::cli::Application application(registry);
    std::istringstream input("pin\nhelp\nquit\n");
    std::ostringstream output;
    Require(application.RunRepl(input, output) == 0, "REPL did not recover from typo");
    Require(output.str().find("Show available commands") != std::string::npos, "HELP after typo did not run");
}

}

int main()
{
    try
    {
        TestCommandsAndExtension();
        TestApplicationModes();
        TestSuggestions();
        std::cout << "CLI registry and application tests passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
