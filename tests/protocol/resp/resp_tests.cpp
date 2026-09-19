#include "gatekeeper/protocol/resp/decoder.h"
#include "gatekeeper/protocol/resp/encoder.h"

#include <cassert>
#include <iostream>

using namespace gatekeeper::protocol::resp;

void TestDecoderArray()
{
    Decoder decoder;
    std::string data = "*3\r\n$3\r\nSET\r\n$4\r\nname\r\n$5\r\nalice\r\n";
    std::vector<Command> cmds;
    std::string err;

    bool ok = decoder.Push(std::span(reinterpret_cast<const std::uint8_t*>(data.data()), data.size()), cmds, err);
    assert(ok);
    assert(cmds.size() == 1);
    assert(cmds[0].args.size() == 3);
    assert(cmds[0].args[0] == "SET");
    assert(cmds[0].args[1] == "name");
    assert(cmds[0].args[2] == "alice");
}

void TestDecoderInline()
{
    Decoder decoder;
    std::string data = "PING\r\n";
    std::vector<Command> cmds;
    std::string err;

    bool ok = decoder.Push(std::span(reinterpret_cast<const std::uint8_t*>(data.data()), data.size()), cmds, err);
    assert(ok);
    assert(cmds.size() == 1);
    assert(cmds[0].args.size() == 1);
    assert(cmds[0].args[0] == "PING");
}

void TestEncoderTranslations()
{
    Command ping_cmd{{"PING"}};
    std::string op;
    std::string json = CommandToJsonRequest(ping_cmd, 1, op);
    assert(op == "PING");
    assert(json.find("\"op\":\"PING\"") != std::string::npos);

    std::string resp = FormatRespResponse("PING", R"({"id":"resp-1","ok":true,"result":{"pong":true}})", 2);
    assert(resp == "+PONG\r\n");

    Command set_cmd{{"SET", "k", "v"}};
    json = CommandToJsonRequest(set_cmd, 2, op);
    assert(op == "SET");
    assert(json.find("\"key\":\"k\"") != std::string::npos);
    assert(json.find("\"value\":\"v\"") != std::string::npos);

    resp = FormatRespResponse("SET", R"({"id":"resp-2","ok":true,"result":{"stored":true}})", 2);
    assert(resp == "+OK\r\n");

    resp = FormatRespResponse("GET", R"({"id":"resp-3","ok":true,"result":{"value":"hello"}})", 2);
    assert(resp == "$5\r\nhello\r\n");

    resp = FormatRespResponse("GET", R"({"id":"resp-4","ok":false,"error":{"code":"NOT_FOUND","message":"missing"}})", 2);
    assert(resp == "$-1\r\n");

    resp = FormatRespResponse("GET", R"({"id":"resp-4","ok":false,"error":{"code":"NOT_FOUND","message":"missing"}})", 3);
    assert(resp == "_\r\n");

    std::string hello = FormatHelloResponse(3);
    assert(hello.rfind("%4\r\n", 0) == 0);
}

int main()
{
    TestDecoderArray();
    TestDecoderInline();
    TestEncoderTranslations();
    std::cout << "All RESP tests passed!" << std::endl;
    return 0;
}
