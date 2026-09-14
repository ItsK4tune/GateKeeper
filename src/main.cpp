#include "server.h"

#include <iostream>

int main()
{
    try
    {
        redismini::Server server(6379);

        server.Run();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Fatal error: " << e.what() << '\n';
        return 1;
    }

    return 0;
}
