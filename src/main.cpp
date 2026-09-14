#include "server.h"

#include <iostream>

int main()
{
    try
    {
        gatekeeper::Server server(63779);
        server.Run();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Fatal error: " << e.what() << '\n';
        return 1;
    }

    return 0;
}
