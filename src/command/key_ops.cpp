#include "gatekeeper/command/key_ops.h"
#include "gatekeeper/protocol/response.h"

#include <cctype>
#include <stdexcept>
#include <string>
#include <vector>

namespace gatekeeper::command
{
namespace
{

class JsonReader
{
public:
    explicit JsonReader(std::string_view input) : input_(input) {}

    void Skip()
    {
        while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_])))
        {
            ++pos_;
        }
    }

    char Peek()
    {
        Skip();
        return pos_ < input_.size() ? input_[pos_] : '\0';
    }

    bool Take(char c)
    {
        Skip();
        if (pos_ < input_.size() && input_[pos_] == c)
        {
            ++pos_;
            return true;
        }
        return false;
    }

    void Expect(char c)
    {
        if (!Take(c))
        {
            throw std::invalid_argument(std::string("expected '") + c + "'");
        }
    }

    void End()
    {
        Skip();
        if (pos_ != input_.size())
        {
            throw std::invalid_argument("unexpected trailing characters");
        }
    }

    std::string String()
    {
        Expect('"');
        std::string out;
        while (pos_ < input_.size())
        {
            char c = input_[pos_++];
            if (c == '"')
            {
                return out;
            }
            if (c == '\\')
            {
                if (pos_ >= input_.size())
                {
                    throw std::invalid_argument("incomplete escape sequence");
                }
                char esc = input_[pos_++];
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
        throw std::invalid_argument("unterminated string");
    }

    std::uint64_t UnsignedNumber()
    {
        Skip();
        if (pos_ >= input_.size() || !std::isdigit(static_cast<unsigned char>(input_[pos_])))
        {
            throw std::invalid_argument("expected unsigned number");
        }
        std::uint64_t val = 0;
        while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_])))
        {
            val = val * 10 + (input_[pos_++] - '0');
        }
        return val;
    }

    std::vector<std::string> StringArray()
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

private:
    std::string_view input_;
    std::size_t pos_{0};
};

Result Del(const protocol::Request& request, storage::Store& store)
{
    JsonReader reader(request.body_json);
    reader.Expect('{');
    std::vector<std::string> keys;
    if (!reader.Take('}'))
    {
        do
        {
            auto field = reader.String();
            reader.Expect(':');
            if (field == "key")
            {
                keys.push_back(reader.String());
            }
            else if (field == "keys")
            {
                auto arr = reader.StringArray();
                keys.insert(keys.end(), arr.begin(), arr.end());
            }
            else
            {
                throw std::invalid_argument("unsupported field: " + field);
            }
            if (reader.Take('}'))
            {
                break;
            }
            reader.Expect(',');
        } while (true);
    }
    reader.End();

    std::size_t deleted = 0;
    for (const auto& key : keys)
    {
        if (store.Del(key))
        {
            ++deleted;
        }
    }
    return {true, "{\"deleted\":" + std::to_string(deleted) + "}", {}};
}

Result Exists(const protocol::Request& request, storage::Store& store)
{
    JsonReader reader(request.body_json);
    reader.Expect('{');
    std::vector<std::string> keys;
    if (!reader.Take('}'))
    {
        do
        {
            auto field = reader.String();
            reader.Expect(':');
            if (field == "key")
            {
                keys.push_back(reader.String());
            }
            else if (field == "keys")
            {
                auto arr = reader.StringArray();
                keys.insert(keys.end(), arr.begin(), arr.end());
            }
            else
            {
                throw std::invalid_argument("unsupported field: " + field);
            }
            if (reader.Take('}'))
            {
                break;
            }
            reader.Expect(',');
        } while (true);
    }
    reader.End();

    std::size_t count = 0;
    for (const auto& key : keys)
    {
        if (store.Exists(key))
        {
            ++count;
        }
    }
    return {true, "{\"count\":" + std::to_string(count) + "}", {}};
}

Result Type(const protocol::Request& request, storage::Store& store)
{
    JsonReader reader(request.body_json);
    reader.Expect('{');
    std::string key;
    reader.Expect('"');
    JsonReader r2(request.body_json);
    r2.Expect('{');
    auto field = r2.String();
    r2.Expect(':');
    if (field != "key")
    {
        throw std::invalid_argument("TYPE requires key field");
    }
    key = r2.String();
    r2.Expect('}');
    r2.End();

    const auto type = store.Type(key);
    std::string type_str = "none";
    if (type == storage::DataType::String)
    {
        type_str = "string";
    }
    return {true, "{\"type\":\"" + type_str + "\"}", {}};
}

Result DbSize(const protocol::Request& request, storage::Store& store)
{
    JsonReader reader(request.body_json);
    reader.Expect('{');
    reader.Expect('}');
    reader.End();

    return {true, "{\"size\":" + std::to_string(store.DbSize()) + "}", {}};
}

Result Keys(const protocol::Request& request, storage::Store& store)
{
    std::string pattern = "*";
    JsonReader reader(request.body_json);
    reader.Expect('{');
    if (!reader.Take('}'))
    {
        auto field = reader.String();
        reader.Expect(':');
        if (field != "pattern")
        {
            throw std::invalid_argument("KEYS supports pattern field");
        }
        pattern = reader.String();
        reader.Expect('}');
    }
    reader.End();

    const auto keys = store.Keys(pattern);
    std::string result = "{\"keys\":[";
    for (std::size_t i = 0; i < keys.size(); ++i)
    {
        if (i > 0) result += ",";
        result += protocol::QuoteJson(keys[i]);
    }
    result += "]}";
    return {true, std::move(result), {}};
}

Result Scan(const protocol::Request& request, storage::Store& store)
{
    std::size_t cursor = 0;
    std::size_t count = 10;
    JsonReader reader(request.body_json);
    reader.Expect('{');
    if (!reader.Take('}'))
    {
        do
        {
            auto field = reader.String();
            reader.Expect(':');
            if (field == "cursor")
            {
                cursor = static_cast<std::size_t>(reader.UnsignedNumber());
            }
            else if (field == "count")
            {
                count = static_cast<std::size_t>(reader.UnsignedNumber());
            }
            else
            {
                throw std::invalid_argument("unsupported SCAN field: " + field);
            }
            if (reader.Take('}'))
            {
                break;
            }
            reader.Expect(',');
        } while (true);
    }
    reader.End();

    auto [next_cursor, keys] = store.Scan(cursor, count);
    std::string result = "{\"cursor\":" + std::to_string(next_cursor) + ",\"keys\":[";
    for (std::size_t i = 0; i < keys.size(); ++i)
    {
        if (i > 0) result += ",";
        result += protocol::QuoteJson(keys[i]);
    }
    result += "]}";
    return {true, std::move(result), {}};
}

auto Validated(Result (*handler)(const protocol::Request&, storage::Store&),
               storage::Store& store)
{
    return [handler, &store](const protocol::Request& request) {
        try
        {
            return handler(request, store);
        }
        catch (const std::exception& error)
        {
            return Result{false, {}, {"INVALID_ARGUMENTS", error.what()}};
        }
    };
}

}

void RegisterKeyOps(Dispatcher& dispatcher, storage::Store& store)
{
    dispatcher.Register("DEL", Validated(Del, store));
    dispatcher.Register("EXISTS", Validated(Exists, store));
    dispatcher.Register("TYPE", Validated(Type, store));
    dispatcher.Register("DBSIZE", Validated(DbSize, store));
    dispatcher.Register("KEYS", Validated(Keys, store));
    dispatcher.Register("SCAN", Validated(Scan, store));
}

}
