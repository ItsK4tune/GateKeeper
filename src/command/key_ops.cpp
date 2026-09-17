#include "gatekeeper/command/key_ops.h"
#include "gatekeeper/protocol/json_reader.h"
#include "gatekeeper/protocol/response.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace gatekeeper::command
{
namespace
{

using protocol::JsonReader;

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
    const auto field = reader.String();
    reader.Expect(':');
    if (field != "key")
    {
        throw std::invalid_argument("TYPE requires key field");
    }
    const auto key = reader.String();
    reader.Expect('}');
    reader.End();

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
