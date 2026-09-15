#include "gatekeeper/net/tcp_server.h"

#include <iostream>

int main()
{
    try
    {
        gatekeeper::net::TcpServer server(63779);
        server.Run();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Fatal error: " << e.what() << '\n';
        return 1;
    }

    return 0;
}
