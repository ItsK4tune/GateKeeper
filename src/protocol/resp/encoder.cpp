#include "gatekeeper/protocol/resp/encoder.h"
#include "gatekeeper/protocol/gkwp/response.h"

#include <algorithm>
#include <cctype>

namespace gatekeeper::protocol::resp
{

std::string FormatSimpleString(std::string_view str)
{
    return "+" + std::string(str) + "\r\n";
}

std::string FormatError(std::string_view msg)
{
    return "-ERR " + std::string(msg) + "\r\n";
}

std::string FormatInteger(long long val)
{
    return ":" + std::to_string(val) + "\r\n";
}

std::string FormatBulkString(std::string_view str)
{
    return "$" + std::to_string(str.size()) + "\r\n" + std::string(str) + "\r\n";
}

std::string FormatNull(int resp_version)
{
    if (resp_version >= 3)
    {
        return "_\r\n";
    }
    return "$-1\r\n";
}

std::string FormatHelloResponse(int /*version*/)
{
    return "%4\r\n"
           "$6\r\nserver\r\n$10\r\ngatekeeper\r\n"
           "$7\r\nversion\r\n$5\r\n1.0.0\r\n"
           "$5\r\nproto\r\n:3\r\n"
           "$4\r\nmode\r\n$10\r\nstandalone\r\n";
}

std::string CommandToJsonRequest(const Command& cmd, std::uint64_t req_id, std::string& out_op)
{
    if (cmd.args.empty())
    {
        return "";
    }

    std::string op = cmd.args[0];
    std::transform(op.begin(), op.end(), op.begin(), [](unsigned char c) { return std::toupper(c); });
    out_op = op;

    const auto id_str = "resp-" + std::to_string(req_id);

    if (op == "PING")
    {
        return "{\"id\":" + QuoteJson(id_str) + ",\"op\":\"PING\",\"body\":{}}";
    }

    if (op == "SET" && cmd.args.size() >= 3)
    {
        std::string body = "{\"key\":" + QuoteJson(cmd.args[1]) + ",\"value\":" + QuoteJson(cmd.args[2]);
        for (std::size_t i = 3; i < cmd.args.size(); ++i)
        {
            std::string opt = cmd.args[i];
            std::transform(opt.begin(), opt.end(), opt.begin(), [](unsigned char c) { return std::toupper(c); });
            if (opt == "NX")
            {
                body += ",\"if_not_exists\":true";
            }
            else if (opt == "XX")
            {
                body += ",\"if_exists\":true";
            }
            else if ((opt == "EX" || opt == "PX") && i + 1 < cmd.args.size())
            {
                try
                {
                    long long val = std::stoll(cmd.args[++i]);
                    long long ms = (opt == "EX") ? val * 1000 : val;
                    body += ",\"ttl_ms\":" + std::to_string(ms);
                }
                catch (...)
                {
                }
            }
        }
        body += "}";
        return "{\"id\":" + QuoteJson(id_str) + ",\"op\":\"SET\",\"body\":" + body + "}";
    }

    if (op == "GET" && cmd.args.size() >= 2)
    {
        return "{\"id\":" + QuoteJson(id_str) + ",\"op\":\"GET\",\"body\":{\"key\":" + QuoteJson(cmd.args[1]) + "}}";
    }

    if (op == "DEL" && cmd.args.size() >= 2)
    {
        return "{\"id\":" + QuoteJson(id_str) + ",\"op\":\"DEL\",\"body\":{\"key\":" + QuoteJson(cmd.args[1]) + "}}";
    }

    if (op == "EXISTS" && cmd.args.size() >= 2)
    {
        return "{\"id\":" + QuoteJson(id_str) + ",\"op\":\"EXISTS\",\"body\":{\"key\":" + QuoteJson(cmd.args[1]) + "}}";
    }

    if (op == "INCR" && cmd.args.size() >= 2)
    {
        return "{\"id\":" + QuoteJson(id_str) + ",\"op\":\"INCR\",\"body\":{\"key\":" + QuoteJson(cmd.args[1]) + "}}";
    }

    if (op == "DECR" && cmd.args.size() >= 2)
    {
        return "{\"id\":" + QuoteJson(id_str) + ",\"op\":\"DECR\",\"body\":{\"key\":" + QuoteJson(cmd.args[1]) + "}}";
    }

    if (op == "TTL" && cmd.args.size() >= 2)
    {
        return "{\"id\":" + QuoteJson(id_str) + ",\"op\":\"TTL\",\"body\":{\"key\":" + QuoteJson(cmd.args[1]) + "}}";
    }

    if (op == "GK.RATE_LIMIT" && cmd.args.size() >= 5)
    {
        return "{\"id\":" + QuoteJson(id_str) + ",\"op\":\"GK.RATE_LIMIT\",\"body\":{\"key\":" +
               QuoteJson(cmd.args[1]) + ",\"limit\":" + cmd.args[2] + ",\"window_ms\":" + cmd.args[3] +
               ",\"cost\":" + cmd.args[4] + "}}";
    }

    return "";
}

std::string FormatRespResponse(std::string_view op, std::string_view json_resp, int resp_version)
{
    const bool is_ok = json_resp.find("\"ok\":true") != std::string_view::npos;

    if (!is_ok)
    {
        if (op == "GET")
        {
            return FormatNull(resp_version);
        }
        std::size_t msg_pos = json_resp.find("\"message\":");
        if (msg_pos != std::string_view::npos)
        {
            std::size_t start = json_resp.find('"', msg_pos + 10);
            if (start != std::string_view::npos)
            {
                std::size_t end = json_resp.find('"', start + 1);
                if (end != std::string_view::npos)
                {
                    return FormatError(json_resp.substr(start + 1, end - start - 1));
                }
            }
        }
        return FormatError("command failed");
    }

    if (op == "PING")
    {
        return "+PONG\r\n";
    }

    if (op == "SET")
    {
        return "+OK\r\n";
    }

    if (op == "GET")
    {
        std::size_t val_pos = json_resp.find("\"value\":");
        if (val_pos != std::string_view::npos)
        {
            std::size_t start = json_resp.find('"', val_pos + 8);
            if (start != std::string_view::npos)
            {
                std::size_t end = start + 1;
                while (end < json_resp.size())
                {
                    if (json_resp[end] == '"' && json_resp[end - 1] != '\\')
                    {
                        break;
                    }
                    ++end;
                }
                if (end < json_resp.size())
                {
                    return FormatBulkString(json_resp.substr(start + 1, end - start - 1));
                }
            }
        }
        return FormatNull(resp_version);
    }

    if (op == "DEL" || op == "EXISTS")
    {
        bool res = json_resp.find(":true") != std::string_view::npos;
        return FormatInteger(res ? 1 : 0);
    }

    if (op == "GK.RATE_LIMIT")
    {
        bool allowed = json_resp.find("\"allowed\":true") != std::string_view::npos;
        return FormatInteger(allowed ? 1 : 0);
    }

    return "+OK\r\n";
}

}
