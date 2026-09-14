#include "server.h"

#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace redismini
{

Server::Server(int port) : port(port), server_fd(-1) {}

void Server::Run()
{
    SetupSocket();

    while (true)
    {
        AcceptSocket();
    }
}

void Server::SetupSocket()
{
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd == -1)
    {
        throw std::runtime_error("Failed to create socket");
    }

    sockaddr_in address{};

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == -1)
    {
        close(server_fd);

        throw std::runtime_error("Failed to bind socket");
    }

    if (listen(server_fd, 16) == -1)
    {
        close(server_fd);

        throw std::runtime_error("Failed to listen");
    }

    std::cout << "Redis server listening on port " << port << '\n';
}

void Server::AcceptSocket()
{
    sockaddr_in client_address{};

    socklen_t client_length = sizeof(client_address);

    int client_fd = accept(server_fd, reinterpret_cast<sockaddr*>(&client_address), &client_length);

    if (client_fd == -1)
    {
        throw std::runtime_error("Failed to accept client");
    }

    std::cout << "Client connected\n";

    char buffer[4096];

    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer));

    if (bytes_read > 0)
    {
        std::cout << "Received " << bytes_read << " bytes\n";

        std::cout.write(buffer, bytes_read);

        std::cout << '\n';
    }

    const char* response = "Server has received data!\r\n";

    write(client_fd, response, std::strlen(response));

    close(client_fd);
}

} // namespace redismini
