#include "gatekeeper/net/server.h"

#include <iostream>
#include <string>

int main(int argc, char* argv[])
{
    try
    {
        if (argc != 2)
        {
            return 2;
        }
        gatekeeper::net::Server server(std::stoi(argv[1]), [](std::string_view) {
            return std::string("{}");
        });
        server.Run();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
