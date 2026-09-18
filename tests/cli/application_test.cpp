#include "gatekeeper/cli/app.h"
#include "gatekeeper/cli/registry.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
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

std::vector<std::string> Variants(const std::string& command)
{
    std::string upper = command;
    std::string lower = command;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return {upper, lower};
}

void TestCommandsAndExtension()
{
    std::vector<std::string> requests;
    gatekeeper::cli::Registry registry([&requests](const std::string& command) {
        requests.push_back(command);
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
        return gatekeeper::cli::Result{arguments.at(0)};
    });
    Require(registry.Execute("eChO DuOnG").output == "DuOnG", "argument case changed");
    Require(registry.Execute("help").output.find("ECHO - Return arguments") != std::string::npos,
            "new command missing from help");
}

void TestApplicationModes()
{
    int requests = 0;
    int sessions = 0;
    gatekeeper::cli::Registry registry([&](const std::string&) {
        ++requests;
        return std::string("PONG");
    });
    gatekeeper::cli::App application(registry, [&sessions]() { ++sessions; });
    for (const auto& word : {"QUIT", "EXIT"})
    {
        for (const auto& variant : Variants(word))
        {
            std::istringstream input("hElP\npInG\n" + variant + "\nPING\n");
            std::ostringstream output;
            const auto previous = requests;
            const auto previous_sessions = sessions;
            Require(application.RunRepl(input, output) == 0, "REPL failed");
            Require(requests == previous + 1, "REPL sent quit or read after exit");
            Require(sessions == previous_sessions + 1, "REPL did not open a session");
            output.str("");
            Require(application.RunCommand(variant, output) == 0, "one-line exit failed");
            Require(output.str().empty() && requests == previous + 1 && sessions == previous_sessions + 1,
                    "one-line exit opened a session");
        }
    }
    std::istringstream input(" \t\n");
    std::ostringstream output;
    const auto previous = requests;
    const auto previous_sessions = sessions;
    Require(application.RunRepl(input, output) == 0 && requests == previous && sessions == previous_sessions + 1,
            "blank input or EOF did not open a session");
    Require(application.RunCommand("pInG", output) == 0 && requests == previous + 1, "one-line ping failed");
}

void TestReplOpensSessionBeforePrompt()
{
    gatekeeper::cli::Registry registry([](const std::string&) { return std::string("PONG"); });
    gatekeeper::cli::App application(registry, []() { throw std::runtime_error("CONNECTION_REFUSED"); });
    std::istringstream input("HELP\n");
    std::ostringstream output;

    try
    {
        application.RunRepl(input, output);
        throw std::runtime_error("REPL accepted an unavailable server");
    }
    catch (const std::runtime_error& error)
    {
        Require(std::string(error.what()) == "CONNECTION_REFUSED", "REPL returned the wrong connection error");
    }
    Require(output.str().empty(), "REPL displayed a prompt before opening the session");
}

void TestSuggestions()
{
    int requests = 0;
    gatekeeper::cli::Registry registry([&](const std::string&) {
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
    registry.Register("PONG", "Test extension", [](const auto&) { return gatekeeper::cli::Result{}; });
    registry.Register("PANG", "Test extension", [](const auto&) { return gatekeeper::cli::Result{}; });
    registry.Register("PUNG", "Test extension", [](const auto&) { return gatekeeper::cli::Result{}; });
    const auto result = registry.Execute("peng");
    Require(result.output.find("Did you mean: PANG, PING, PONG?") != std::string::npos, "suggestion ranking or limit failed");
    Require(requests == 0, "suggestions executed remote commands");
    gatekeeper::cli::App application(registry, []() {});
    std::istringstream input("pin\nhelp\nquit\n");
    std::ostringstream output;
    Require(application.RunRepl(input, output) == 0, "REPL did not recover from typo");
    Require(output.str().find("Show available commands") != std::string::npos, "HELP after typo did not run");
}

}

#include "gatekeeper/cli/format.h"
#include <cassert>

namespace {
void TestFormatResponse()
{
    // Test PING
    auto r1 = gatekeeper::cli::FormatResponse(R"({"id":"gate-1","ok":true,"result":{"pong":true}})");
    assert(r1.output == "PONG");
    assert(r1.exit_code == 0);

    // Test SET ok
    auto r2 = gatekeeper::cli::FormatResponse(R"({"id":"gate-1","ok":true,"result":{"stored":true}})");
    assert(r2.output == "OK");

    // Test SET false
    auto r3 = gatekeeper::cli::FormatResponse(R"({"id":"gate-1","ok":true,"result":{"stored":false}})");
    assert(r3.output == "(nil)");

    // Test GET value
    auto r4 = gatekeeper::cli::FormatResponse(R"({"id":"gate-1","ok":true,"result":{"value":"duong"}})");
    assert(r4.output == "\"duong\"");

    // Test error
    auto r5 = gatekeeper::cli::FormatResponse(R"({"id":"gate-1","ok":false,"error":{"code":"KEY_NOT_FOUND","message":"key does not exist"}})");
    assert(r5.output == "(error) KEY_NOT_FOUND: key does not exist");
    assert(r5.exit_code != 0);

    // Test non-json
    auto r6 = gatekeeper::cli::FormatResponse("RAW_TEXT");
    assert(r6.output == "RAW_TEXT");
}
}

int main()
{
    TestFormatResponse();
    try
    {
        TestCommandsAndExtension();
        TestApplicationModes();
        TestReplOpensSessionBeforePrompt();
        TestSuggestions();
        std::cout << "CLI registry and application tests passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
