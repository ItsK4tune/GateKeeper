#include "gatekeeper/net/channel.h"
#include "gatekeeper/protocol/gkwp2/frame.h"

#include <array>
#include <cerrno>
#include <chrono>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace gatekeeper::net
{

Channel::Channel(int fd, EventLoop& loop, RequestHandler handler, std::shared_ptr<log::Logger> logger, std::uint64_t session_id, std::string address, DisconnectCallback on_disconnect, SessionRequestHandler session_handler)
    : fd_(fd),
      loop_(loop),
      handler_(std::move(handler)),
      logger_(std::move(logger)),
      session_(session_id, fd, std::move(address)),
      on_disconnect_(std::move(on_disconnect)),
      session_handler_(std::move(session_handler))
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
        if (on_disconnect_) { on_disconnect_(session_.Id()); }
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
            std::vector<protocol::gkwp2::Frame> frames;
            std::string error;
            if (!session_.Push(std::span(buffer.data(), static_cast<std::size_t>(received)), frames, error))
            {
                const auto err_frame = protocol::gkwp2::EncodeError(0, 0, "PROTOCOL_ERROR", error, true);
                Send(err_frame);
                Close();
                return;
            }
            for (const auto& frame : frames)
            {
                const auto stream_id = frame.header.stream_id;
                if (stream_id != 0)
                {
                    if (!session_.StreamManager().CanOpenStream(stream_id))
                    {
                        const auto err_frame = protocol::gkwp2::EncodeError(
                            frame.header.request_id, stream_id, "TOO_MANY_STREAMS",
                            "Maximum concurrent streams exceeded", true);
                        Send(err_frame);
                        continue;
                    }
                    const auto now_ms = static_cast<std::uint64_t>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch()).count());
                    session_.StreamManager().GetOrCreateStream(stream_id, now_ms);
                }

                session_.IncrementRequests();
                const auto response = session_handler_ ? session_handler_(frame.payload, session_.Id()) : handler_(frame.payload);
                const bool is_end_stream = (frame.header.flags & protocol::gkwp2::flags::kEndStream) != 0;
                const auto resp_frame = protocol::gkwp2::EncodeResponse(
                    frame.header.request_id, stream_id, response, is_end_stream);
                Send(resp_frame);

                if (stream_id != 0 && is_end_stream)
                {
                    session_.StreamManager().CloseStream(stream_id);
                }

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
    if (write_offset_ == write_buffer_.size())
    {
        write_buffer_.clear();
        write_offset_ = 0;
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

    if (write_buffer_.size() - write_offset_ + data.size() > kMaxWriteBufferSize)
    {
        logger_->Error("Write buffer limit exceeded for fd=" + std::to_string(fd_) + "; closing connection");
        Close();
        return;
    }

    write_buffer_.insert(write_buffer_.end(), data.begin(), data.end());
    loop_.Modify(fd_, EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLERR);
}

void Channel::HandleWrite()
{
    while (!closed_ && write_offset_ < write_buffer_.size())
    {
        const auto remaining = write_buffer_.size() - write_offset_;
        const auto written = send(fd_, write_buffer_.data() + write_offset_, remaining, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (written > 0)
        {
            session_.AddBytesSent(static_cast<std::size_t>(written));
            write_offset_ += static_cast<std::size_t>(written);
            if (write_offset_ == write_buffer_.size())
            {
                write_buffer_.clear();
                write_offset_ = 0;
                break;
            }
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
    if (!closed_ && write_offset_ == write_buffer_.size())
    {
        write_buffer_.clear();
        write_offset_ = 0;
        loop_.Modify(fd_, EPOLLIN | EPOLLRDHUP | EPOLLERR);
    }
}

}
