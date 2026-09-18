#include "gatekeeper/net/http_channel.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace gatekeeper::net
{

HttpChannel::HttpChannel(int fd, EventLoop& loop, HttpHandler handler,
                         std::shared_ptr<log::Logger> logger, std::string address)
    : fd_(fd),
      loop_(loop),
      handler_(std::move(handler)),
      logger_(std::move(logger)),
      address_(std::move(address))
{
    if (!logger_)
    {
        logger_ = log::Logger::Null();
    }
}

HttpChannel::~HttpChannel()
{
    Close();
}

void HttpChannel::Close()
{
    if (!closed_)
    {
        closed_ = true;
        loop_.Remove(fd_);
        logger_->Info("Closing HTTP channel connection fd=" + std::to_string(fd_));
        close(fd_);
    }
}

void HttpChannel::HandleEvents(std::uint32_t events)
{
    if (closed_)
    {
        return;
    }
    if (events & (EPOLLIN | EPOLLPRI))
    {
        HandleRead();
    }
    if (!closed_ && (events & EPOLLOUT))
    {
        HandleWrite();
    }
    if (!closed_ && (events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)))
    {
        Close();
    }
}

void HttpChannel::HandleRead()
{
    std::array<char, 4096> buffer{};
    while (!closed_)
    {
        const auto received = recv(fd_, buffer.data(), buffer.size(), MSG_DONTWAIT);
        if (received > 0)
        {
            read_buffer_.append(buffer.data(), static_cast<std::size_t>(received));
            ProcessReadBuffer();
        }
        else if (received == 0)
        {
            Close();
            return;
        }
        else
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            if (errno == EINTR)
            {
                continue;
            }
            Close();
            return;
        }
    }
}

void HttpChannel::ProcessReadBuffer()
{
    while (!closed_)
    {
        const auto header_end = read_buffer_.find("\r\n\r\n");
        if (header_end == std::string::npos)
        {
            if (read_buffer_.size() > 16384)
            {
                HttpResponse res{431, "Request Header Fields Too Large", {}, "{\"error\":\"HEADER_TOO_LARGE\"}"};
                res.SetHeader("Connection", "close");
                res.SetHeader("Content-Type", "application/json");
                res.SetHeader("Content-Length", std::to_string(res.body.size()));
                close_after_write_ = true;
                Send(res.Serialize());
            }
            return;
        }

        std::string_view headers_sv(read_buffer_.data(), header_end);
        const auto first_line_end = headers_sv.find("\r\n");
        if (first_line_end == std::string_view::npos)
        {
            Close();
            return;
        }
        std::string_view request_line = headers_sv.substr(0, first_line_end);

        const auto first_space = request_line.find(' ');
        if (first_space == std::string_view::npos)
        {
            Close();
            return;
        }
        const auto second_space = request_line.find(' ', first_space + 1);
        if (second_space == std::string_view::npos)
        {
            Close();
            return;
        }

        HttpRequest req;
        req.method = std::string(request_line.substr(0, first_space));
        req.uri = std::string(request_line.substr(first_space + 1, second_space - first_space - 1));
        req.version = std::string(request_line.substr(second_space + 1));

        const auto q_pos = req.uri.find('?');
        if (q_pos != std::string::npos)
        {
            req.path = req.uri.substr(0, q_pos);
            req.query = req.uri.substr(q_pos + 1);
        }
        else
        {
            req.path = req.uri;
        }

        std::size_t content_length = 0;
        std::size_t line_start = first_line_end + 2;
        while (line_start < header_end)
        {
            auto line_end = headers_sv.find("\r\n", line_start);
            if (line_end == std::string_view::npos)
            {
                line_end = header_end;
            }
            std::string_view line = headers_sv.substr(line_start, line_end - line_start);
            const auto colon = line.find(':');
            if (colon != std::string_view::npos)
            {
                std::string header_name;
                header_name.reserve(colon);
                for (std::size_t i = 0; i < colon; ++i)
                {
                    header_name.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(line[i]))));
                }
                std::string_view header_val = line.substr(colon + 1);
                while (!header_val.empty() && std::isspace(static_cast<unsigned char>(header_val.front())))
                {
                    header_val.remove_prefix(1);
                }
                while (!header_val.empty() && std::isspace(static_cast<unsigned char>(header_val.back())))
                {
                    header_val.remove_suffix(1);
                }

                req.headers.emplace(header_name, std::string(header_val));
                if (header_name == "content-length")
                {
                    try
                    {
                        content_length = std::stoull(std::string(header_val));
                    }
                    catch (...)
                    {
                        content_length = 0;
                    }
                }
            }
            line_start = line_end + 2;
        }

        const std::size_t total_request_len = header_end + 4 + content_length;
        if (read_buffer_.size() < total_request_len)
        {
            return;
        }

        req.body = read_buffer_.substr(header_end + 4, content_length);
        read_buffer_.erase(0, total_request_len);

        bool keep_alive = true;
        const auto conn_hdr = req.GetHeader("connection");
        if (req.version == "HTTP/1.0")
        {
            keep_alive = (conn_hdr == "keep-alive");
        }
        else
        {
            keep_alive = (conn_hdr != "close");
        }

        HttpResponse res;
        if (handler_)
        {
            res = handler_(req);
        }
        else
        {
            res.status_code = 500;
            res.status_text = "Internal Server Error";
            res.body = "{\"error\":\"NO_HANDLER\"}";
        }

        res.SetHeader("Server", "GateKeeper/1.0");
        if (!keep_alive)
        {
            res.SetHeader("Connection", "close");
            close_after_write_ = true;
        }
        else
        {
            res.SetHeader("Connection", "keep-alive");
        }
        if (!res.HasHeader("Content-Type"))
        {
            res.SetHeader("Content-Type", "application/json");
        }
        res.SetHeader("Content-Length", std::to_string(res.body.size()));

        Send(res.Serialize());
        if (close_after_write_ && write_buffer_.empty())
        {
            Close();
            return;
        }
    }
}

void HttpChannel::Send(std::string_view data)
{
    if (closed_)
    {
        return;
    }
    if (write_buffer_.empty())
    {
        const auto written = send(fd_, data.data(), data.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
        if (written > 0)
        {
            if (static_cast<std::size_t>(written) == data.size())
            {
                if (close_after_write_)
                {
                    Close();
                }
                return;
            }
            data = data.substr(static_cast<std::size_t>(written));
        }
        else if (written == -1 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
        {
            Close();
            return;
        }
    }
    write_buffer_.insert(write_buffer_.end(), data.begin(), data.end());
    loop_.Modify(fd_, EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLERR);
}

void HttpChannel::HandleWrite()
{
    while (!closed_ && !write_buffer_.empty())
    {
        const auto written = send(fd_, write_buffer_.data(), write_buffer_.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
        if (written > 0)
        {
            write_buffer_.erase(write_buffer_.begin(), write_buffer_.begin() + written);
        }
        else if (written == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            if (errno == EINTR)
            {
                continue;
            }
            Close();
            return;
        }
    }
    if (!closed_ && write_buffer_.empty())
    {
        if (close_after_write_)
        {
            Close();
            return;
        }
        loop_.Modify(fd_, EPOLLIN | EPOLLRDHUP | EPOLLERR);
    }
}

}
