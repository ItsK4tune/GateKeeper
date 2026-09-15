#include "gatekeeper/cli/command.h"

#include <cassert>

namespace
{

void TestNormalizesMixedCaseCliCommands()
{
    assert(gatekeeper::cli::NormalizeCommand("Help") == "HELP");
    assert(gatekeeper::cli::NormalizeCommand("HElP") == "HELP");
    assert(gatekeeper::cli::NormalizeCommand("hElp") == "HELP");
    assert(gatekeeper::cli::NormalizeCommand("qUiT") == "QUIT");
    assert(gatekeeper::cli::NormalizeCommand("eXiT") == "EXIT");
}

}

void RunCliCommandTests()
{
    TestNormalizesMixedCaseCliCommands();
}
