#include "protocol.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string_view>

namespace gatekeeper::protocol
{
namespace
{

bool IsValidUtf8(std::string_view text)
{
    for (std::size_t i = 0; i < text.size();)
    {
        const auto byte = static_cast<unsigned char>(text[i]);
        std::size_t continuation_count = 0;

        if (byte <= 0x7F)
        {
            ++i;
            continue;
        }
        if (byte >= 0xC2 && byte <= 0xDF)
        {
            continuation_count = 1;
        }
        else if (byte >= 0xE0 && byte <= 0xEF)
        {
            continuation_count = 2;
        }
        else if (byte >= 0xF0 && byte <= 0xF4)
        {
            continuation_count = 3;
        }
        else
        {
            return false;
        }

        if (i + continuation_count >= text.size())
        {
            return false;
        }
        for (std::size_t j = 1; j <= continuation_count; ++j)
        {
            if ((static_cast<unsigned char>(text[i + j]) & 0xC0U) != 0x80U)
            {
                return false;
            }
        }

        // Reject overlong encodings, UTF-16 surrogate code points, and code points above U+10FFFF.
        if ((byte == 0xE0 && static_cast<unsigned char>(text[i + 1]) < 0xA0) ||
            (byte == 0xED && static_cast<unsigned char>(text[i + 1]) > 0x9F) ||
            (byte == 0xF0 && static_cast<unsigned char>(text[i + 1]) < 0x90) ||
            (byte == 0xF4 && static_cast<unsigned char>(text[i + 1]) > 0x8F))
        {
            return false;
        }
        i += continuation_count + 1;
    }
    return true;
}

std::string EscapeJson(std::string_view text)
{
    std::string escaped;
    escaped.reserve(text.size());
    constexpr char hex[] = "0123456789abcdef";

    for (unsigned char character : text)
    {
        switch (character)
        {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\b': escaped += "\\b"; break;
        case '\f': escaped += "\\f"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if (character < 0x20U)
            {
                escaped += "\\u00";
                escaped += hex[character >> 4U];
                escaped += hex[character & 0x0FU];
            }
            else
            {
                escaped += static_cast<char>(character);
            }
        }
    }
    return escaped;
}

class JsonParser
{
  public:
    explicit JsonParser(std::string_view input) : input_(input) {}

    bool ParseRequestObject(Request& request, std::string& message)
    {
        SkipWhitespace();
        if (!Consume('{'))
        {
            message = "request must be a JSON object";
            return false;
        }

        bool has_id = false;
        bool has_op = false;
        bool has_body = false;
        SkipWhitespace();
        if (Consume('}'))
        {
            message = "request is missing id, op, and body";
            return false;
        }

        while (true)
        {
            std::string key;
            if (!ParseString(key) || !Consume(':'))
            {
                message = "invalid JSON object member";
                return false;
            }
            SkipWhitespace();

            if (key == "id" || key == "op")
            {
                std::string value;
                if (!ParseString(value))
                {
                    message = key + " must be a string";
                    return false;
                }
                if (key == "id")
                {
                    if (has_id)
                    {
                        message = "id must not be repeated";
                        return false;
                    }
                    request.id = std::move(value);
                    has_id = true;
                }
                else
                {
                    if (has_op)
                    {
                        message = "op must not be repeated";
                        return false;
                    }
                    request.op = std::move(value);
                    has_op = true;
                }
            }
            else
            {
                const std::size_t value_start = position_;
                if (!SkipValue())
                {
                    message = "invalid JSON value";
                    return false;
                }
                if (key == "body")
                {
                    if (has_body || input_[value_start] != '{')
                    {
                        message = "body must be one JSON object";
                        return false;
                    }
                    request.body_json = std::string(input_.substr(value_start, position_ - value_start));
                    has_body = true;
                }
            }

            SkipWhitespace();
            if (Consume('}'))
            {
                break;
            }
            if (!Consume(','))
            {
                message = "expected ',' or '}'";
                return false;
            }
            SkipWhitespace();
        }

        SkipWhitespace();
        if (position_ != input_.size())
        {
            message = "unexpected data after request";
            return false;
        }
        if (!has_id || request.id.empty() || !has_op || request.op.empty() || !has_body)
        {
            message = "request requires non-empty id, non-empty op, and object body";
            return false;
        }
        return true;
    }

  private:
    void SkipWhitespace()
    {
        while (position_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[position_])) != 0)
        {
            ++position_;
        }
    }

    bool Consume(char expected)
    {
        SkipWhitespace();
        if (position_ == input_.size() || input_[position_] != expected)
        {
            return false;
        }
        ++position_;
        return true;
    }

    bool ParseString(std::string& output)
    {
        SkipWhitespace();
        if (position_ == input_.size() || input_[position_++] != '"')
        {
            return false;
        }
        output.clear();
        while (position_ < input_.size())
        {
            const char character = input_[position_++];
            if (character == '"')
            {
                return true;
            }
            if (static_cast<unsigned char>(character) < 0x20U)
            {
                return false;
            }
            if (character != '\\')
            {
                output += character;
                continue;
            }
            if (position_ == input_.size())
            {
                return false;
            }
            const char escaped = input_[position_++];
            switch (escaped)
            {
            case '"': output += '"'; break;
            case '\\': output += '\\'; break;
            case '/': output += '/'; break;
            case 'b': output += '\b'; break;
            case 'f': output += '\f'; break;
            case 'n': output += '\n'; break;
            case 'r': output += '\r'; break;
            case 't': output += '\t'; break;
            case 'u':
                if (position_ + 4 > input_.size() ||
                    !std::all_of(input_.begin() + static_cast<std::ptrdiff_t>(position_),
                                 input_.begin() + static_cast<std::ptrdiff_t>(position_ + 4), [](char c) {
                                     return std::isxdigit(static_cast<unsigned char>(c)) != 0;
                                 }))
                {
                    return false;
                }
                // Keep escapes in their JSON form; command fields only need stable text validation here.
                output += "\\u";
                output.append(input_.substr(position_, 4));
                position_ += 4;
                break;
            default: return false;
            }
        }
        return false;
    }

    bool SkipValue()
    {
        SkipWhitespace();
        if (position_ == input_.size())
        {
            return false;
        }
        const char first = input_[position_];
        if (first == '"')
        {
            std::string ignored;
            return ParseString(ignored);
        }
        if (first == '{')
        {
            ++position_;
            SkipWhitespace();
            if (Consume('}'))
            {
                return true;
            }
            while (true)
            {
                std::string key;
                if (!ParseString(key) || !Consume(':') || !SkipValue())
                {
                    return false;
                }
                SkipWhitespace();
                if (Consume('}'))
                {
                    return true;
                }
                if (!Consume(','))
                {
                    return false;
                }
            }
        }
        if (first == '[')
        {
            ++position_;
            SkipWhitespace();
            if (Consume(']'))
            {
                return true;
            }
            while (true)
            {
                if (!SkipValue())
                {
                    return false;
                }
                SkipWhitespace();
                if (Consume(']'))
                {
                    return true;
                }
                if (!Consume(','))
                {
                    return false;
                }
            }
        }
        const std::size_t start = position_;
        while (position_ < input_.size() && !std::isspace(static_cast<unsigned char>(input_[position_])) &&
               input_[position_] != ',' && input_[position_] != '}' && input_[position_] != ']')
        {
            ++position_;
        }
        const std::string_view token = input_.substr(start, position_ - start);
        if (token == "true" || token == "false" || token == "null")
        {
            return true;
        }
        bool has_digit = false;
        for (char character : token)
        {
            if (std::isdigit(static_cast<unsigned char>(character)) != 0)
            {
                has_digit = true;
            }
            else if (character != '-' && character != '+' && character != '.' && character != 'e' &&
                     character != 'E')
            {
                return false;
            }
        }
        return has_digit;
    }

    std::string_view input_;
    std::size_t position_ = 0;
};

} // namespace

bool FrameDecoder::Push(std::span<const std::uint8_t> bytes, std::vector<std::string>& frames,
                        ProtocolError& error)
{
    buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());

    while (buffer_.size() >= sizeof(std::uint32_t))
    {
        const auto length = (static_cast<std::uint32_t>(buffer_[0]) << 24U) |
                            (static_cast<std::uint32_t>(buffer_[1]) << 16U) |
                            (static_cast<std::uint32_t>(buffer_[2]) << 8U) |
                            static_cast<std::uint32_t>(buffer_[3]);
        if (length > kMaxFrameSize)
        {
            error = {"FRAME_TOO_LARGE", "payload exceeds the 1 MiB GKWP limit"};
            return false;
        }
        const std::size_t frame_size = sizeof(std::uint32_t) + static_cast<std::size_t>(length);
        if (buffer_.size() < frame_size)
        {
            return true;
        }

        std::string payload(buffer_.begin() + static_cast<std::ptrdiff_t>(sizeof(std::uint32_t)),
                            buffer_.begin() + static_cast<std::ptrdiff_t>(frame_size));
        if (!IsValidUtf8(payload))
        {
            error = {"INVALID_UTF8", "payload must be valid UTF-8"};
            return false;
        }
        frames.push_back(std::move(payload));
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(frame_size));
    }
    return true;
}

bool ParseRequest(const std::string& payload, Request& request, ProtocolError& error)
{
    request = {};
    JsonParser parser(payload);
    std::string message;
    if (!parser.ParseRequestObject(request, message))
    {
        error = {"INVALID_REQUEST", std::move(message)};
        return false;
    }
    return true;
}

std::vector<std::uint8_t> EncodeFrame(const std::string& payload)
{
    if (payload.size() > kMaxFrameSize || payload.size() > std::numeric_limits<std::uint32_t>::max())
    {
        return {};
    }
    const auto length = static_cast<std::uint32_t>(payload.size());
    std::vector<std::uint8_t> frame;
    frame.reserve(sizeof(length) + payload.size());
    frame.push_back(static_cast<std::uint8_t>(length >> 24U));
    frame.push_back(static_cast<std::uint8_t>(length >> 16U));
    frame.push_back(static_cast<std::uint8_t>(length >> 8U));
    frame.push_back(static_cast<std::uint8_t>(length));
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

std::string EncodeSuccess(const std::string& id, const std::string& result_json)
{
    return "{\"id\":\"" + EscapeJson(id) + "\",\"ok\":true,\"result\":" + result_json + "}";
}

std::string EncodeError(const std::string& id, const ProtocolError& error)
{
    return "{\"id\":\"" + EscapeJson(id) + "\",\"ok\":false,\"error\":{\"code\":\"" +
           EscapeJson(error.code) + "\",\"message\":\"" + EscapeJson(error.message) + "\"}}";
}

} // namespace gatekeeper::protocol
