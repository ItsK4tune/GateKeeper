#include "gatekeeper/net/connection.h"
#include "gatekeeper/protocol/gkwp/frame.h"

#include <array>
#include <iostream>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace
{

void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void TestConnection(bool handler_throws)
{
    int sockets[2];
    Require(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0, "socketpair failed");
    const gatekeeper::net::RequestHandler handler = [handler_throws](std::string_view payload) {
        Require(payload == "input", "transport altered request");
        if (handler_throws)
        {
            throw std::runtime_error("handler failure");
        }
        return std::string("output");
    };
    try
    {
        const auto frame = gatekeeper::protocol::EncodeFrame("input");
        Require(send(sockets[1], frame.data(), frame.size(), 0) == static_cast<ssize_t>(frame.size()), "write failed");
        shutdown(sockets[1], SHUT_WR);
        bool threw = false;
        try
        {
            gatekeeper::net::Connection connection(sockets[0], handler);
            connection.Serve();
        }
        catch (const std::runtime_error&)
        {
            threw = true;
        }
        Require(threw == handler_throws, "unexpected handler outcome");
        std::array<std::uint8_t, 64> bytes{};
        std::vector<std::uint8_t> response;
        for (ssize_t received; (received = recv(sockets[1], bytes.data(), bytes.size(), 0)) > 0;)
        {
            response.insert(response.end(), bytes.begin(), bytes.begin() + received);
        }
        Require(handler_throws ? response.empty() : response == gatekeeper::protocol::EncodeFrame("output"),
                "response differs from injected handler");
        close(sockets[1]);
    }
    catch (...)
    {
        close(sockets[1]);
        throw;
    }
}

}

int main()
{
    try
    {
        TestConnection(false);
        TestConnection(true);
        std::cout << "Connection injection and cleanup tests passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
