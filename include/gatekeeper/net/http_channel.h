#pragma once

#include "gatekeeper/log/logger.h"
#include "gatekeeper/net/event_loop.h"
#include "gatekeeper/net/http_types.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gatekeeper::net
{

using HttpHandler = std::function<HttpResponse(const HttpRequest&)>;

class HttpChannel
{
public:
    HttpChannel(int fd, EventLoop& loop, HttpHandler handler,
                std::shared_ptr<log::Logger> logger = log::Logger::Null(),
                std::string address = "");
    ~HttpChannel();

    HttpChannel(const HttpChannel&) = delete;
    HttpChannel& operator=(const HttpChannel&) = delete;

    void HandleEvents(std::uint32_t events);
    void HandleRead();
    void HandleWrite();
    void Send(std::string_view data);
    void Close();

    [[nodiscard]] bool IsClosed() const noexcept { return closed_; }
    [[nodiscard]] int Fd() const noexcept { return fd_; }
    [[nodiscard]] const std::string& Address() const noexcept { return address_; }

private:
    int fd_;
    EventLoop& loop_;
    HttpHandler handler_;
    std::shared_ptr<log::Logger> logger_;
    std::string address_;
    std::string read_buffer_;
    std::vector<char> write_buffer_;
    bool closed_{false};
    bool close_after_write_{false};

    void ProcessReadBuffer();
};

}
