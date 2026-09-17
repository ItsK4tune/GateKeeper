#include "gatekeeper/cli/format.h"
#include "gatekeeper/protocol/response.h"

#include <cctype>
#include <string>
#include <string_view>

namespace gatekeeper::cli
{
namespace
{

void SkipWs(std::string_view value, std::size_t& pos)
{
    while (pos < value.size() && std::isspace(static_cast<unsigned char>(value[pos])))
    {
        ++pos;
    }
}

bool ParseJsonString(std::string_view value, std::size_t& pos, std::string& out)
{
    SkipWs(value, pos);
    if (pos >= value.size() || value[pos++] != '"')
    {
        return false;
    }
    out.clear();
    while (pos < value.size())
    {
        char c = value[pos++];
        if (c == '"')
        {
            return true;
        }
        if (c == '\\')
        {
            if (pos >= value.size())
            {
                return false;
            }
            char esc = value[pos++];
            if (esc == '"' || esc == '\\' || esc == '/') out += esc;
            else if (esc == 'b') out += '\b';
            else if (esc == 'f') out += '\f';
            else if (esc == 'n') out += '\n';
            else if (esc == 'r') out += '\r';
            else if (esc == 't') out += '\t';
            else out += esc;
        }
        else
        {
            out += c;
        }
    }
    return false;
}

bool ParseBool(std::string_view value, std::size_t& pos, bool& out)
{
    SkipWs(value, pos);
    if (pos + 4 <= value.size() && value.substr(pos, 4) == "true")
    {
        out = true;
        pos += 4;
        return true;
    }
    if (pos + 5 <= value.size() && value.substr(pos, 5) == "false")
    {
        out = false;
        pos += 5;
        return true;
    }
    return false;
}

}

Result FormatResponse(std::string_view gkwp_json)
{
    if (gkwp_json.empty())
    {
        return {};
    }
    // If it is not JSON (e.g. mock test returning "PONG"), return directly as success
    if (gkwp_json.front() != '{')
    {
        return Result{std::string(gkwp_json)};
    }

    // Parse simple GKWP fields: ok, result, error
    bool has_ok = false;
    bool ok_value = false;
    std::string error_message;
    std::string error_code;

    // Check if ok: true or false
    auto ok_pos = gkwp_json.find("\"ok\":");
    if (ok_pos != std::string_view::npos)
    {
        ok_pos += 5;
        if (ParseBool(gkwp_json, ok_pos, ok_value))
        {
            has_ok = true;
        }
    }

    if (!has_ok)
    {
        return Result{std::string(gkwp_json)};
    }

    if (!ok_value)
    {
        // Parse error message and code
        auto msg_pos = gkwp_json.find("\"message\":");
        if (msg_pos != std::string_view::npos)
        {
            msg_pos += 10;
            ParseJsonString(gkwp_json, msg_pos, error_message);
        }
        auto code_pos = gkwp_json.find("\"code\":");
        if (code_pos != std::string_view::npos)
        {
            code_pos += 7;
            ParseJsonString(gkwp_json, code_pos, error_code);
        }

        std::string err_text = "(error)";
        if (!error_code.empty())
        {
            err_text += " " + error_code;
        }
        if (!error_message.empty())
        {
            err_text += ": " + error_message;
        }
        return Result{std::move(err_text), false, 1};
    }

    // Success response: parse result object
    // 1. PING -> {"pong": true}
    if (gkwp_json.find("\"pong\":true") != std::string_view::npos ||
        gkwp_json.find("\"pong\": true") != std::string_view::npos)
    {
        return Result{"PONG"};
    }

    // 2. SET -> {"stored": true} or {"stored": false}
    if (gkwp_json.find("\"stored\":true") != std::string_view::npos ||
        gkwp_json.find("\"stored\": true") != std::string_view::npos)
    {
        return Result{"OK"};
    }
    if (gkwp_json.find("\"stored\":false") != std::string_view::npos ||
        gkwp_json.find("\"stored\": false") != std::string_view::npos)
    {
        return Result{"(nil)"};
    }

    // 3. GET -> {"value": "..."}
    auto val_pos = gkwp_json.find("\"value\":");
    if (val_pos != std::string_view::npos)
    {
        val_pos += 8;
        std::string value;
        if (ParseJsonString(gkwp_json, val_pos, value))
        {
            return Result{protocol::QuoteJson(value)};
        }
    }

    // Fallback: if unrecognized result format, return raw JSON
    return Result{std::string(gkwp_json)};
}

}

