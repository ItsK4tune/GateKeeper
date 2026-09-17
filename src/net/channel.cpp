#include "gatekeeper/net/channel.h"
#include "gatekeeper/protocol/frame.h"
#include "gatekeeper/protocol/response.h"

#include <array>
#include <cerrno>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace gatekeeper::net
{

Channel::Channel(int fd, EventLoop& loop, RequestHandler handler, std::shared_ptr<log::Logger> logger, std::uint64_t session_id, std::string address)
    : fd_(fd),
      loop_(loop),
      handler_(std::move(handler)),
      logger_(std::move(logger)),
      session_(session_id, fd, std::move(address))
{
    if (!logger_)
    {
        logger_ = log::Logger::Null();
    }
}

Channel::~Channel()
{
    Close();
}

void Channel::Close()
{
    if (!closed_)
    {
        closed_ = true;
        loop_.Remove(fd_);
        logger_->Info("Closing channel connection fd=" + std::to_string(fd_));
        close(fd_);
    }
}

void Channel::HandleEvents(std::uint32_t events)
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

void Channel::HandleRead()
{
    std::array<std::uint8_t, 4096> buffer{};
    while (!closed_)
    {
        const auto received = recv(fd_, buffer.data(), buffer.size(), MSG_DONTWAIT);
        if (received > 0)
        {
            session_.AddBytesReceived(static_cast<std::size_t>(received));
            std::vector<std::string> payloads;
            protocol::Error error;
            if (!session_.Decoder().Push(std::span(buffer.data(), static_cast<std::size_t>(received)), payloads, error))
            {
                const auto err_response = protocol::EncodeErrorResponse("", error);
                Send(protocol::EncodeFrame(err_response));
                Close();
                return;
            }
            for (const auto& payload : payloads)
            {
                session_.IncrementRequests();
                const auto response = handler_(payload);
                Send(protocol::EncodeFrame(response));
                if (closed_)
                {
                    return;
                }
            }
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

void Channel::Send(std::span<const std::uint8_t> data)
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
            session_.AddBytesSent(static_cast<std::size_t>(written));
            if (static_cast<std::size_t>(written) == data.size())
            {
                return;
            }
            data = data.subspan(static_cast<std::size_t>(written));
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

void Channel::HandleWrite()
{
    while (!closed_ && !write_buffer_.empty())
    {
        const auto written = send(fd_, write_buffer_.data(), write_buffer_.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
        if (written > 0)
        {
            session_.AddBytesSent(static_cast<std::size_t>(written));
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
        loop_.Modify(fd_, EPOLLIN | EPOLLRDHUP | EPOLLERR);
    }
}

}
