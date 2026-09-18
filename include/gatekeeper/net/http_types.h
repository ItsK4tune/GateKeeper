#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gatekeeper::net
{

struct HttpRequest
{
    std::string method;
    std::string uri;
    std::string path;
    std::string query;
    std::string version{"HTTP/1.1"};
    std::unordered_map<std::string, std::string> headers;
    std::string body;

    [[nodiscard]] std::string_view GetHeader(std::string_view name) const noexcept
    {
        const auto it = headers.find(std::string(name));
        if (it != headers.end())
        {
            return it->second;
        }
        return {};
    }
};

struct HttpResponse
{
    int status_code{200};
    std::string status_text{"OK"};
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;

    void SetHeader(std::string name, std::string value)
    {
        for (auto& [k, v] : headers)
        {
            if (k == name)
            {
                v = std::move(value);
                return;
            }
        }
        headers.emplace_back(std::move(name), std::move(value));
    }

    [[nodiscard]] bool HasHeader(std::string_view name) const noexcept
    {
        for (const auto& [k, v] : headers)
        {
            if (k == name)
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::string Serialize() const
    {
        std::string out;
        out.reserve(128 + headers.size() * 32 + body.size());
        out += "HTTP/1.1 ";
        out += std::to_string(status_code);
        out += " ";
        out += status_text;
        out += "\r\n";
        for (const auto& [k, v] : headers)
        {
            out += k;
            out += ": ";
            out += v;
            out += "\r\n";
        }
        out += "\r\n";
        out += body;
        return out;
    }
};

}
