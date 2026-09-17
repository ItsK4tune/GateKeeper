#include "gatekeeper/protocol/response.h"

namespace gatekeeper::protocol
{

std::string QuoteJson(std::string_view value)
{
    std::string out = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : value)
    {
        switch (c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (c < 32)
            {
                out += "\\u00";
                out += hex[c >> 4];
                out += hex[c & 15];
            }
            else
            {
                out += static_cast<char>(c);
            }
        }
    }
    return out + '"';
}

std::string EncodeSuccessResponse(const std::string& id, const std::string& result_json)
{
    return "{\"id\":" + QuoteJson(id) + ",\"ok\":true,\"result\":" + result_json + "}";
}

std::string EncodeErrorResponse(const std::string& id, const ProtocolError& error)
{
    return "{\"id\":" + QuoteJson(id) + ",\"ok\":false,\"error\":{\"code\":" +
           QuoteJson(error.code) + ",\"message\":" + QuoteJson(error.message) + "}}";
}

}
