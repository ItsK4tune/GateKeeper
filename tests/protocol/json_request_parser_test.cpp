#include "gatekeeper/protocol/parser.h"

#include <cassert>

namespace
{

void TestParserCreatesARequestFromAValidPayload()
{
    gatekeeper::protocol::Parser parser;
    gatekeeper::protocol::Request request;
    gatekeeper::protocol::ProtocolError error;

    assert(parser.Parse(R"({"id":"request-1","op":"PING","body":{}})", request, error));
    assert(request.id == "request-1");
    assert(request.op == "PING");
    assert(request.body_json == "{}");
}

void TestParserRejectsANonObjectBody()
{
    gatekeeper::protocol::Parser parser;
    gatekeeper::protocol::Request request;
    gatekeeper::protocol::ProtocolError error;

    assert(!parser.Parse(R"({"id":"request-1","op":"PING","body":[]})", request, error));
    assert(error.code == "INVALID_REQUEST");
}

}

void RunJsonRequestParserTests()
{
    TestParserCreatesARequestFromAValidPayload();
    TestParserRejectsANonObjectBody();
}
