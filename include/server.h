#pragma once

namespace gatekeeper
{

class Server
{
  public:
    explicit Server(int port);
    void Run();

  private:
    int port;
    int server_fd;

    void SetupSocket();
    void AcceptSocket();
};

} // namespace gatekeeper
