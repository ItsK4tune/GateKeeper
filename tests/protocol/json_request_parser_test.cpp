#include "gatekeeper/protocol/json_reader.h"
#include "gatekeeper/protocol/json_parser.h"

#include <cassert>
#include <stdexcept>

namespace
{

void TestParserCreatesARequestFromAValidPayload()
{
    gatekeeper::protocol::Parser parser;
    gatekeeper::protocol::Request request;
    gatekeeper::protocol::Error error;

    assert(parser.Parse(R"({"id":"request-1","op":"PING","body":{}})", request, error));
    assert(request.id == "request-1");
    assert(request.op == "PING");
    assert(request.body_json == "{}");
}

void TestParserRejectsANonObjectBody()
{
    gatekeeper::protocol::Parser parser;
    gatekeeper::protocol::Request request;
    gatekeeper::protocol::Error error;

    assert(!parser.Parse(R"({"id":"request-1","op":"PING","body":[]})", request, error));
    assert(error.code == "INVALID_REQUEST");
}

void TestJsonReaderPrimitives()
{
    gatekeeper::protocol::JsonReader r1(R"({"key":"hello\nworld","num":12345,"delta":-42,"flag":true,"arr":["x","y"]})");
    r1.Expect('{');
    assert(r1.String() == "key");
    r1.Expect(':');
    assert(r1.String() == "hello\nworld");
    r1.Expect(',');
    assert(r1.String() == "num");
    r1.Expect(':');
    assert(r1.UnsignedNumber() == 12345);
    r1.Expect(',');
    assert(r1.String() == "delta");
    r1.Expect(':');
    assert(r1.SignedNumber() == -42);
    r1.Expect(',');
    assert(r1.String() == "flag");
    r1.Expect(':');
    assert(r1.Boolean() == true);
    r1.Expect(',');
    assert(r1.String() == "arr");
    r1.Expect(':');
    const auto arr = r1.StringArray();
    assert(arr.size() == 2 && arr[0] == "x" && arr[1] == "y");
    r1.Expect('}');
    r1.End();
}

void TestJsonReaderErrors()
{
    bool failed = false;
    try
    {
        gatekeeper::protocol::JsonReader r(R"("unterminated)");
        r.String();
    }
    catch (const std::invalid_argument&)
    {
        failed = true;
    }
    assert(failed);

    failed = false;
    try
    {
        gatekeeper::protocol::JsonReader r(R"(abc)");
        r.UnsignedNumber();
    }
    catch (const std::invalid_argument&)
    {
        failed = true;
    }
    assert(failed);
}

}

void RunJsonRequestParserTests()
{
    TestParserCreatesARequestFromAValidPayload();
    TestParserRejectsANonObjectBody();
    TestJsonReaderPrimitives();
    TestJsonReaderErrors();
}
