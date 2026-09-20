#include "gatekeeper/service/processor.h"
#include "gatekeeper/storage/memory_store.h"
#include "gatekeeper/protocol/gkwp2/binary_codec.h"

#include <iostream>
#include <stdexcept>

namespace
{

void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

}

int main()
{
    try
    {
        gatekeeper::storage::MemoryStore store;
        gatekeeper::service::Processor processor(&store);

        // 1. Test Ping
        const auto ping_req = gatekeeper::protocol::gkwp2::BinaryCodec::EncodePing();
        const auto ping_resp = processor.Process(ping_req);
        Require(!ping_resp.empty() && ping_resp[0] == 0, "Ping failed");
        Require(ping_resp.substr(3) == "PONG", "Ping not PONG");

        // 2. Test Set & Get
        const auto set_req = gatekeeper::protocol::gkwp2::BinaryCodec::EncodeSet("foo", "bar");
        const auto set_resp = processor.Process(set_req);
        Require(!set_resp.empty() && set_resp[0] == 0, "Set failed");

        const auto get_req = gatekeeper::protocol::gkwp2::BinaryCodec::EncodeGet("foo");
        const auto get_resp = processor.Process(get_req);
        Require(!get_resp.empty() && get_resp[0] == 0, "Get failed");
        Require(get_resp.substr(5) == "bar", "Get value mismatch");

        // 3. Test Rate Limit
        const auto rl_req = gatekeeper::protocol::gkwp2::BinaryCodec::EncodeRateLimit("user:1", 10, 60000, 1);
        const auto rl_resp = processor.Process(rl_req);
        Require(!rl_resp.empty() && rl_resp[0] == 0, "RateLimit failed");
        Require(rl_resp[1] == 1, "RateLimit not allowed");

        // 4. Test Del
        const auto del_req = gatekeeper::protocol::gkwp2::BinaryCodec::EncodeDel("foo");
        const auto del_resp = processor.Process(del_req);
        Require(!del_resp.empty() && del_resp[0] == 0, "Del failed");
        Require(del_resp[1] == 1, "Del not 1");

        // 5. Test Get after Del (NotFound)
        const auto get2_resp = processor.Process(get_req);
        Require(!get2_resp.empty() && get2_resp[0] == static_cast<char>(gatekeeper::protocol::gkwp2::BinaryStatus::NotFound), "Get not NotFound");

        std::cout << "Binary request processor tests passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
