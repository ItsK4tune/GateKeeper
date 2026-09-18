#include <cstdint>
#include "gatekeeper/cli/format.h"
#include "gatekeeper/protocol/response.h"

#include <cctype>
#include <iomanip>
#include <sstream>
#include <vector>

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

bool ParseNumber(std::string_view value, std::size_t& pos, std::int64_t& out)
{
    SkipWs(value, pos);
    if (pos >= value.size())
    {
        return false;
    }
    bool neg = false;
    if (value[pos] == '-')
    {
        neg = true;
        ++pos;
    }
    if (pos >= value.size() || !std::isdigit(static_cast<unsigned char>(value[pos])))
    {
        return false;
    }
    out = 0;
    while (pos < value.size() && std::isdigit(static_cast<unsigned char>(value[pos])))
    {
        out = out * 10 + (value[pos++] - '0');
    }
    if (neg) out = -out;
    return true;
}

bool ParseStringList(std::string_view value, std::size_t& pos, std::vector<std::string>& out)
{
    SkipWs(value, pos);
    if (pos >= value.size() || value[pos++] != '[')
    {
        return false;
    }
    out.clear();
    SkipWs(value, pos);
    if (pos < value.size() && value[pos] == ']')
    {
        ++pos;
        return true;
    }
    while (pos < value.size())
    {
        std::string s;
        if (!ParseJsonString(value, pos, s))
        {
            return false;
        }
        out.push_back(std::move(s));
        SkipWs(value, pos);
        if (pos < value.size() && value[pos] == ']')
        {
            ++pos;
            return true;
        }
        if (pos < value.size() && value[pos] == ',')
        {
            ++pos;
            continue;
        }
        return false;
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
    if (gkwp_json.front() != '{')
    {
        return Result{std::string(gkwp_json)};
    }

    bool has_ok = false;
    bool ok_value = false;
    std::string error_message;
    std::string error_code;

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

    if (gkwp_json.find("\"pong\":true") != std::string_view::npos ||
        gkwp_json.find("\"pong\": true") != std::string_view::npos)
    {
        return Result{"PONG"};
    }

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

    // Rate limit output formatting
    auto allowed_pos = gkwp_json.find("\"allowed\":");
    if (allowed_pos != std::string_view::npos)
    {
        allowed_pos += 10;
        bool allowed_val = false;
        ParseBool(gkwp_json, allowed_pos, allowed_val);
        std::int64_t remaining_val = 0;
        auto rem_pos = gkwp_json.find("\"remaining\":");
        if (rem_pos != std::string_view::npos)
        {
            rem_pos += 12;
            ParseNumber(gkwp_json, rem_pos, remaining_val);
        }
        std::int64_t retry_val = 0;
        auto ret_pos = gkwp_json.find("\"retry_after_ms\":");
        if (ret_pos != std::string_view::npos)
        {
            ret_pos += 17;
            ParseNumber(gkwp_json, ret_pos, retry_val);
        }
        return Result{"allowed=" + std::string(allowed_val ? "1" : "0") +
                      " remaining=" + std::to_string(remaining_val) +
                      " retry_after_ms=" + std::to_string(retry_val)};
    }

    // Reservation responses
    if (gkwp_json.find("\"reserved\":") != std::string_view::npos)
    {
        std::string res_id;
        auto id_pos = gkwp_json.find("\"reservation_id\":");
        if (id_pos != std::string_view::npos)
        {
            id_pos += 17;
            ParseJsonString(gkwp_json, id_pos, res_id);
        }
        std::int64_t rem = 0;
        auto rem_pos = gkwp_json.find("\"remaining\":");
        if (rem_pos != std::string_view::npos)
        {
            rem_pos += 12;
            ParseNumber(gkwp_json, rem_pos, rem);
        }
        return Result{"reserved=1 reservation_id=" + protocol::QuoteJson(res_id) + " remaining=" + std::to_string(rem)};
    }

    if (gkwp_json.find("\"committed\":") != std::string_view::npos)
    {
        std::int64_t act = 0, ref = 0, rem = 0;
        auto act_pos = gkwp_json.find("\"actual_amount\":");
        if (act_pos != std::string_view::npos) { act_pos += 16; ParseNumber(gkwp_json, act_pos, act); }
        auto ref_pos = gkwp_json.find("\"refunded\":");
        if (ref_pos != std::string_view::npos) { ref_pos += 11; ParseNumber(gkwp_json, ref_pos, ref); }
        auto rem_pos = gkwp_json.find("\"remaining\":");
        if (rem_pos != std::string_view::npos) { rem_pos += 12; ParseNumber(gkwp_json, rem_pos, rem); }
        return Result{"committed=1 actual_amount=" + std::to_string(act) + " refunded=" + std::to_string(ref) + " remaining=" + std::to_string(rem)};
    }

    if (gkwp_json.find("\"rolled_back\":") != std::string_view::npos)
    {
        std::int64_t ref = 0, rem = 0;
        auto ref_pos = gkwp_json.find("\"refunded\":");
        if (ref_pos != std::string_view::npos) { ref_pos += 11; ParseNumber(gkwp_json, ref_pos, ref); }
        auto rem_pos = gkwp_json.find("\"remaining\":");
        if (rem_pos != std::string_view::npos) { rem_pos += 12; ParseNumber(gkwp_json, rem_pos, rem); }
        return Result{"rolled_back=1 refunded=" + std::to_string(ref) + " remaining=" + std::to_string(rem)};
    }

    for (const char* bool_field : {"\"set\":", "\"persisted\":", "\"initialized\":"})
    {
        auto pos = gkwp_json.find(bool_field);
        if (pos != std::string_view::npos)
        {
            pos += std::string_view(bool_field).size();
            bool b = false;
            if (ParseBool(gkwp_json, pos, b))
            {
                return Result{b ? "(integer) 1" : "(integer) 0"};
            }
        }
    }

    auto val_pos = gkwp_json.find("\"value\":");
    if (val_pos != std::string_view::npos)
    {
        val_pos += 8;
        std::string value;
        if (ParseJsonString(gkwp_json, val_pos, value))
        {
            return Result{protocol::QuoteJson(value)};
        }
        std::int64_t num = 0;
        if (ParseNumber(gkwp_json, val_pos, num))
        {
            return Result{"(integer) " + std::to_string(num)};
        }
    }

    for (const char* int_field : {"\"deleted\":", "\"count\":", "\"size\":", "\"ttl_seconds\":", "\"ttl_ms\":", "\"balance\":"})
    {
        auto pos = gkwp_json.find(int_field);
        if (pos != std::string_view::npos)
        {
            pos += std::string_view(int_field).size();
            std::int64_t n = 0;
            if (ParseNumber(gkwp_json, pos, n))
            {
                return Result{"(integer) " + std::to_string(n)};
            }
        }
    }

    auto type_pos = gkwp_json.find("\"type\":");
    if (type_pos != std::string_view::npos)
    {
        type_pos += 7;
        std::string type_val;
        if (ParseJsonString(gkwp_json, type_pos, type_val))
        {
            return Result{type_val};
        }
    }

    auto scan_pos = gkwp_json.find("\"cursor\":");
    if (scan_pos != std::string_view::npos)
    {
        scan_pos += 9;
        std::int64_t next_cursor = 0;
        if (ParseNumber(gkwp_json, scan_pos, next_cursor))
        {
            auto keys_pos = gkwp_json.find("\"keys\":");
            if (keys_pos != std::string_view::npos)
            {
                keys_pos += 7;
                std::vector<std::string> keys;
                if (ParseStringList(gkwp_json, keys_pos, keys))
                {
                    std::string out = "1) \"" + std::to_string(next_cursor) + "\"\n2) ";
                    if (keys.empty())
                    {
                        out += "(empty list or set)";
                    }
                    else
                    {
                        for (std::size_t i = 0; i < keys.size(); ++i)
                        {
                            if (i > 0) out += "\n   ";
                            out += std::to_string(i + 1) + ") " + protocol::QuoteJson(keys[i]);
                        }
                    }
                    return Result{std::move(out)};
                }
            }
        }
    }

    auto keys_pos = gkwp_json.find("\"keys\":");
    if (keys_pos != std::string_view::npos)
    {
        keys_pos += 7;
        std::vector<std::string> keys;
        if (ParseStringList(gkwp_json, keys_pos, keys))
        {
            if (keys.empty())
            {
                return Result{"(empty list or set)"};
            }
            std::string out;
            for (std::size_t i = 0; i < keys.size(); ++i)
            {
                if (i > 0) out += '\n';
                out += std::to_string(i + 1) + ") " + protocol::QuoteJson(keys[i]);
            }
            return Result{std::move(out)};
        }
    }

    return Result{std::string(gkwp_json)};
}

}
