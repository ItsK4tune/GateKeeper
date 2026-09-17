#include "gatekeeper/protocol/string_body.h"
#include <stdexcept>
namespace gatekeeper::protocol {
namespace {
class Reader {
public:
    explicit Reader(std::string_view input) : input_(input) {}
    StringBody Read() {
        StringBody result;
        Expect('{');
        if (Take('}')) { End(); return result; }
        do {
            auto key = String();
            Expect(':');
            BodyValue value;
            Skip();
            if (Peek() == '"') value = String();
            else if (Literal("true")) value = true;
            else if (Literal("false")) value = false;
            else Fail();
            if (!result.emplace(std::move(key), std::move(value)).second) Fail();
            if (Take('}')) { End(); return result; }
            Expect(',');
        } while (true);
    }
private:
    [[noreturn]] static void Fail() { throw std::invalid_argument("body must contain unique string or boolean fields"); }
    char Peek() const { return pos_ < input_.size() ? input_[pos_] : '\0'; }
    void Skip() { while (pos_ < input_.size() && (Peek() == ' ' || Peek() == '\t' || Peek() == '\n' || Peek() == '\r')) ++pos_; }
    bool Take(char c) { Skip(); if (pos_ < input_.size() && Peek() == c) { ++pos_; return true; } return false; }
    void Expect(char c) { if (!Take(c)) Fail(); }
    void End() { Skip(); if (pos_ != input_.size()) Fail(); }
    bool Literal(std::string_view text) {
        if (input_.substr(pos_, text.size()) != text) return false;
        pos_ += text.size(); return true;
    }
    unsigned Hex() {
        unsigned value = 0;
        for (int i = 0; i < 4; ++i) {
            if (pos_ == input_.size()) Fail();
            const char c = input_[pos_++];
            value *= 16;
            if (c >= '0' && c <= '9') value += c - '0';
            else if (c >= 'a' && c <= 'f') value += c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') value += c - 'A' + 10;
            else Fail();
        }
        return value;
    }
    static void Utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 63));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 63));
            out += static_cast<char>(0x80 | (cp & 63));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 63));
            out += static_cast<char>(0x80 | ((cp >> 6) & 63));
            out += static_cast<char>(0x80 | (cp & 63));
        }
    }
    std::string String() {
        Expect('"');
        std::string out;
        while (pos_ < input_.size()) {
            unsigned char c = input_[pos_++];
            if (c == '"') return out;
            if (c < 0x20) Fail();
            if (c != '\\') { out += static_cast<char>(c); continue; }
            if (pos_ == input_.size()) Fail();
            switch (input_[pos_++]) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp = Hex();
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        if (!Literal("\\u")) Fail();
                        unsigned low = Hex();
                        if (low < 0xDC00 || low > 0xDFFF) Fail();
                        cp = 0x10000 + ((cp - 0xD800) << 10) + low - 0xDC00;
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) Fail();
                    Utf8(out, cp); break;
                }
                default: Fail();
            }
        }
        Fail();
    }
    std::string_view input_;
    std::size_t pos_ = 0;
};
}
StringBody ParseStringBody(std::string_view json) { return Reader(json).Read(); }
std::string QuoteJson(std::string_view value) {
    std::string out = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
                else out += static_cast<char>(c);
        }
    }
    return out + '"';
}
}
