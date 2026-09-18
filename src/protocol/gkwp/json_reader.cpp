#include "gatekeeper/protocol/gkwp/json_reader.h"

#include <cctype>
#include <stdexcept>

namespace gatekeeper::protocol
{

JsonReader::JsonReader(std::string_view input)
    : input_(input)
{
}

void JsonReader::Skip()
{
    while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_])))
    {
        ++pos_;
    }
}

char JsonReader::Peek()
{
    Skip();
    return pos_ < input_.size() ? input_[pos_] : '\0';
}

bool JsonReader::Take(char c)
{
    Skip();
    if (pos_ < input_.size() && input_[pos_] == c)
    {
        ++pos_;
        return true;
    }
    return false;
}

void JsonReader::Expect(char c)
{
    if (!Take(c))
    {
        Fail(std::string("expected '") + c + "'");
    }
}

void JsonReader::End()
{
    Skip();
    if (pos_ != input_.size())
    {
        Fail("unexpected trailing characters");
    }
}

void JsonReader::Fail(const std::string& message)
{
    throw std::invalid_argument(message);
}

unsigned JsonReader::Hex()
{
    unsigned value = 0;
    for (int i = 0; i < 4; ++i)
    {
        if (pos_ == input_.size())
        {
            Fail("incomplete unicode escape");
        }
        const char c = input_[pos_++];
        value *= 16;
        if (c >= '0' && c <= '9') value += c - '0';
        else if (c >= 'a' && c <= 'f') value += c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') value += c - 'A' + 10;
        else Fail("invalid hex in unicode escape");
    }
    return value;
}

void JsonReader::AppendUtf8(std::string& out, unsigned code_point)
{
    if (code_point < 0x80)
    {
        out += static_cast<char>(code_point);
    }
    else if (code_point < 0x800)
    {
        out += static_cast<char>(0xC0 | (code_point >> 6));
        out += static_cast<char>(0x80 | (code_point & 0x3F));
    }
    else if (code_point < 0x10000)
    {
        out += static_cast<char>(0xE0 | (code_point >> 12));
        out += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code_point & 0x3F));
    }
    else
    {
        out += static_cast<char>(0xF0 | (code_point >> 18));
        out += static_cast<char>(0x80 | ((code_point >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code_point & 0x3F));
    }
}

std::string JsonReader::String()
{
    Expect('"');
    std::string out;
    while (pos_ < input_.size())
    {
        const unsigned char c = static_cast<unsigned char>(input_[pos_++]);
        if (c == '"')
        {
            return out;
        }
        if (c == '\\')
        {
            if (pos_ >= input_.size())
            {
                Fail("incomplete escape sequence");
            }
            const char esc = input_[pos_++];
            switch (esc)
            {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u':
                {
                    unsigned cp = Hex();
                    if (cp >= 0xD800 && cp <= 0xDBFF)
                    {
                        if (pos_ + 2 > input_.size() || input_[pos_] != '\\' || input_[pos_ + 1] != 'u')
                        {
                            Fail("missing low surrogate in unicode escape");
                        }
                        pos_ += 2;
                        const unsigned low = Hex();
                        if (low < 0xDC00 || low > 0xDFFF)
                        {
                            Fail("invalid low surrogate in unicode escape");
                        }
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    }
                    AppendUtf8(out, cp);
                    break;
                }
                default:
                    Fail(std::string("invalid escape sequence: \\") + esc);
            }
        }
        else
        {
            out += static_cast<char>(c);
        }
    }
    Fail("unterminated string");
}

std::uint64_t JsonReader::UnsignedNumber()
{
    Skip();
    if (pos_ >= input_.size() || !std::isdigit(static_cast<unsigned char>(input_[pos_])))
    {
        Fail("expected unsigned number");
    }
    std::uint64_t val = 0;
    while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_])))
    {
        val = val * 10 + (input_[pos_++] - '0');
    }
    return val;
}

std::int64_t JsonReader::SignedNumber()
{
    Skip();
    if (pos_ >= input_.size())
    {
        Fail("expected signed number");
    }
    bool negative = false;
    if (input_[pos_] == '-')
    {
        negative = true;
        ++pos_;
    }
    else if (input_[pos_] == '+')
    {
        ++pos_;
    }
    const auto uval = UnsignedNumber();
    if (negative)
    {
        return -static_cast<std::int64_t>(uval);
    }
    return static_cast<std::int64_t>(uval);
}

bool JsonReader::Boolean()
{
    Skip();
    if (input_.substr(pos_, 4) == "true")
    {
        pos_ += 4;
        return true;
    }
    if (input_.substr(pos_, 5) == "false")
    {
        pos_ += 5;
        return false;
    }
    Fail("expected boolean value");
}

std::vector<std::string> JsonReader::StringArray()
{
    Expect('[');
    std::vector<std::string> arr;
    if (Take(']'))
    {
        return arr;
    }
    do
    {
        arr.push_back(String());
        if (Take(']'))
        {
            return arr;
        }
        Expect(',');
    } while (true);
}

}
