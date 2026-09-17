#include "gatekeeper/net/session.h"
#include "gatekeeper/storage/memory_store.h"

#include <atomic>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{

void Require(bool ok, const char* message)
{
    if (!ok)
    {
        throw std::runtime_error(message);
    }
}

void TestSessionState()
{
    gatekeeper::net::Session session(101, 5, "127.0.0.1:45678");
    Require(session.Id() == 101, "wrong session id");
    Require(session.Fd() == 5, "wrong session fd");
    Require(session.Address() == "127.0.0.1:45678", "wrong session address");
    Require(session.RequestsCount() == 0, "initial requests not 0");

    session.IncrementRequests();
    session.IncrementRequests();
    Require(session.RequestsCount() == 2, "requests count not 2");

    session.AddBytesReceived(100);
    session.AddBytesSent(200);
    Require(session.BytesReceived() == 100, "bytes received wrong");
    Require(session.BytesSent() == 200, "bytes sent wrong");
}

void TestConcurrentReadWrite()
{
    gatekeeper::storage::MemoryStore store;
    store.Set("counter", "0", gatekeeper::storage::WriteCondition::Always);

    constexpr int num_threads = 8;
    constexpr int ops_per_thread = 500;
    std::vector<std::thread> threads;
    std::atomic<bool> start{false};

    for (int t = 0; t < num_threads; ++t)
    {
        threads.emplace_back([&store, &start, t]() {
            while (!start.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            for (int i = 0; i < ops_per_thread; ++i)
            {
                if (i % 2 == 0)
                {
                    store.Set("counter", std::to_string(t * ops_per_thread + i),
                              gatekeeper::storage::WriteCondition::Always);
                }
                else
                {
                    auto val = store.Get("counter");
                    Require(val.has_value(), "value must exist");
                }
            }
        });
    }

    start.store(true, std::memory_order_release);
    for (auto& th : threads)
    {
        th.join();
    }

    auto final_val = store.Get("counter");
    Require(final_val.has_value(), "final counter value missing");
    Require(store.Exists("counter"), "counter key does not exist");
}

}

int main()
{
    try
    {
        TestSessionState();
        TestConcurrentReadWrite();
        std::cout << "Concurrent clients tests passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
