#include "gatekeeper/net/channel.h"
#include "gatekeeper/net/event_loop.h"
#include "gatekeeper/protocol/gkwp/frame.h"

#include <array>
#include <fcntl.h>
#include <sys/epoll.h>
#include <iostream>
#include <stdexcept>
#include <string>
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

void SetNonBlocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void TestEventLoopAndChannel()
{
    int sp[2];
    Require(socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0, "socketpair failed");
    SetNonBlocking(sp[0]);
    SetNonBlocking(sp[1]);

    gatekeeper::net::EventLoop loop;
    bool called = false;
    const gatekeeper::net::RequestHandler handler = [&called](std::string_view payload) {
        called = true;
        Require(payload == "ping", "handler payload wrong");
        return std::string("pong");
    };

    auto channel = std::make_unique<gatekeeper::net::Channel>(sp[0], loop, handler, nullptr, 1, "test");
    auto* ch_ptr = channel.get();

    loop.Add(sp[0], EPOLLIN | EPOLLRDHUP | EPOLLERR, [ch_ptr](std::uint32_t ev) {
        ch_ptr->HandleEvents(ev);
    });

    const auto frame = gatekeeper::protocol::EncodeFrame("ping");

    for (std::size_t i = 0; i < frame.size(); ++i)
    {
        Require(write(sp[1], &frame[i], 1) == 1, "partial write failed");
        loop.RunOnce(10);
    }

    Require(called, "handler not called after full frame received");

    std::array<std::uint8_t, 64> resp_buf{};
    const auto n = read(sp[1], resp_buf.data(), resp_buf.size());
    Require(n > 0, "response not received");

    const auto expected = gatekeeper::protocol::EncodeFrame("pong");
    Require(n == static_cast<ssize_t>(expected.size()), "response size wrong");

    channel->Close();
    close(sp[1]);
}

void TestEventLoopPeriodicTimer()
{
    gatekeeper::net::EventLoop loop;
    Require(!loop.HasTimer(), "initial timer state wrong");
    Require(loop.GetTimerInterval() == -1, "initial timer interval wrong");

    int tick_count = 0;
    loop.SetPeriodicTimer(10, [&loop, &tick_count]() {
        ++tick_count;
        if (tick_count >= 3)
        {
            loop.Stop();
        }
    });

    Require(loop.HasTimer(), "timer should be active");
    Require(loop.GetTimerInterval() == 10, "timer interval wrong");

    loop.Run();

    Require(tick_count >= 3, "timer should have ticked at least 3 times");

    loop.SetPeriodicTimer(-1, nullptr);
    Require(!loop.HasTimer(), "timer should be inactive after reset");
    Require(loop.GetTimerInterval() == -1, "timer interval should be -1");
}

}

int main()
{
    try
    {
        TestEventLoopAndChannel();
        TestEventLoopPeriodicTimer();
        std::cout << "EventLoop and Channel tests passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
