#include "gatekeeper/protocol/parser.h"

#include <cctype>
#include <string>

namespace gatekeeper::protocol
{
namespace
{

void Skip(std::string_view value, std::size_t& position)
{
    while (position < value.size() && std::isspace(static_cast<unsigned char>(value[position]))) ++position;
}

bool String(std::string_view value, std::size_t& position, std::string& output)
{
    Skip(value, position);
    if (position == value.size() || value[position++] != '"') return false;
    output.clear();
    while (position < value.size())
    {
        const char character = value[position++];
        if (character == '"') return true;
        if (character < 0x20) return false;
        if (character != '\\') { output += character; continue; }
        if (position == value.size()) return false;
        const char escaped = value[position++];
        if (escaped == '"' || escaped == '\\' || escaped == '/') output += escaped;
        else if (escaped == 'b') output += '\b'; else if (escaped == 'f') output += '\f';
        else if (escaped == 'n') output += '\n'; else if (escaped == 'r') output += '\r';
        else if (escaped == 't') output += '\t';
        else return false;
    }
    return false;
}

bool Object(std::string_view value, std::size_t& position, std::string* captured)
{
    Skip(value, position);
    const auto start = position;
    if (position == value.size() || value[position++] != '{') return false;
    int depth = 1;
    bool quoted = false;
    while (position < value.size() && depth != 0)
    {
        const char character = value[position++];
        if (quoted && character == '\\') { if (position == value.size()) return false; ++position; continue; }
        if (character == '"') quoted = !quoted;
        else if (!quoted && character == '{') ++depth;
        else if (!quoted && character == '}') --depth;
    }
    if (depth != 0 || quoted) return false;
    if (captured != nullptr) *captured = std::string(value.substr(start, position - start));
    return true;
}

}

bool Parser::Parse(std::string_view payload, Request& request, Error& error) const
{
    request = {};
    std::size_t position = 0;
    Skip(payload, position);
    if (position == payload.size() || payload[position++] != '{') { error = {"INVALID_REQUEST", "request must be a JSON object"}; return false; }
    bool id = false, op = false, body = false;
    while (true)
    {
        Skip(payload, position);
        if (position < payload.size() && payload[position] == '}') { ++position; break; }
        std::string key, value;
        if (!String(payload, position, key)) { error = {"INVALID_REQUEST", "invalid JSON object member"}; return false; }
        Skip(payload, position);
        if (position == payload.size() || payload[position++] != ':') { error = {"INVALID_REQUEST", "invalid JSON object member"}; return false; }
        if (key == "id" || key == "op")
        {
            if (!String(payload, position, value)) { error = {"INVALID_REQUEST", key + " must be a string"}; return false; }
            if (key == "id") { if (id) { error = {"INVALID_REQUEST", "id must not be repeated"}; return false; } request.id = value; id = true; }
            else { if (op) { error = {"INVALID_REQUEST", "op must not be repeated"}; return false; } request.op = value; op = true; }
        }
        else if (key == "body")
        {
            if (body || !Object(payload, position, &request.body_json)) { error = {"INVALID_REQUEST", "body must be one JSON object"}; return false; }
            body = true;
        }
        else { std::string ignored; if (!String(payload, position, ignored) && !Object(payload, position, nullptr)) { error = {"INVALID_REQUEST", "invalid JSON value"}; return false; } }
        Skip(payload, position);
        if (position < payload.size() && payload[position] == ',') { ++position; continue; }
        if (position < payload.size() && payload[position] == '}') { ++position; break; }
        error = {"INVALID_REQUEST", "expected ',' or '}'"}; return false;
    }
    Skip(payload, position);
    if (position != payload.size() || !id || request.id.empty() || !op || request.op.empty() || !body) { error = {"INVALID_REQUEST", "request requires non-empty id, non-empty op, and object body"}; return false; }
    return true;
}

}
