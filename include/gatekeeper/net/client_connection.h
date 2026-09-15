#pragma once

namespace gatekeeper::net
{

class ClientConnection
{
public:
    explicit ClientConnection(int client_fd);
    void Serve();

private:
    int client_fd_;

    bool SendAll(const unsigned char* bytes, unsigned long size);
    bool SendError(const char* id, const char* code, const char* message);
};

}
