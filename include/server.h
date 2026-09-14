#pragma once

namespace redismini{

class Server{
    public:
        explicit Server(int port);

        void Run();

    private:
        int port;
        int server_fd;

        void SetupSocket();
        void AcceptSocket();
};

}
