#include "gatekeeper/protocol/resp/decoder.h"

#include <cctype>
#include <cstring>
#include <string>

namespace gatekeeper::protocol::resp
{

bool Decoder::Push(std::span<const std::uint8_t> bytes, std::vector<Command>& commands, std::string& error_msg)
{
    buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());

    while (read_offset_ < buffer_.size())
    {
        const char* buf = reinterpret_cast<const char*>(buffer_.data() + read_offset_);
        std::size_t avail = buffer_.size() - read_offset_;

        if (avail == 0)
        {
            break;
        }

        if (buf[0] == '*')
        {
            const char* crlf = static_cast<const char*>(std::memchr(buf, '\r', avail));
            if (!crlf || crlf + 1 >= buf + avail || *(crlf + 1) != '\n')
            {
                break;
            }

            std::size_t line_len = crlf - buf;
            long count = 0;
            try
            {
                count = std::stol(std::string(buf + 1, line_len - 1));
            }
            catch (...)
            {
                error_msg = "Invalid RESP array length";
                return false;
            }

            if (count <= 0)
            {
                read_offset_ += line_len + 2;
                continue;
            }

            std::size_t offset = line_len + 2;
            std::vector<std::string> args;
            args.reserve(count);
            bool complete = true;

            for (long i = 0; i < count; ++i)
            {
                if (offset >= avail || buf[offset] != '$')
                {
                    complete = false;
                    break;
                }
                const char* bulk_crlf = static_cast<const char*>(std::memchr(buf + offset, '\r', avail - offset));
                if (!bulk_crlf || bulk_crlf + 1 >= buf + avail || *(bulk_crlf + 1) != '\n')
                {
                    complete = false;
                    break;
                }

                std::size_t bulk_line_len = bulk_crlf - (buf + offset);
                long bulk_len = 0;
                try
                {
                    bulk_len = std::stol(std::string(buf + offset + 1, bulk_line_len - 1));
                }
                catch (...)
                {
                    error_msg = "Invalid bulk string length";
                    return false;
                }

                std::size_t data_start = offset + bulk_line_len + 2;
                if (bulk_len == -1)
                {
                    args.emplace_back("");
                    offset = data_start;
                    continue;
                }

                if (data_start + bulk_len + 2 > avail)
                {
                    complete = false;
                    break;
                }

                if (buf[data_start + bulk_len] != '\r' || buf[data_start + bulk_len + 1] != '\n')
                {
                    error_msg = "Bulk string missing trailing CRLF";
                    return false;
                }

                args.emplace_back(buf + data_start, bulk_len);
                offset = data_start + bulk_len + 2;
            }

            if (!complete)
            {
                break;
            }

            Command cmd;
            cmd.args = std::move(args);
            commands.push_back(std::move(cmd));
            read_offset_ += offset;
        }
        else
        {
            const char* crlf = static_cast<const char*>(std::memchr(buf, '\r', avail));
            if (!crlf || crlf + 1 >= buf + avail || *(crlf + 1) != '\n')
            {
                break;
            }

            std::size_t line_len = crlf - buf;
            std::string line(buf, line_len);
            read_offset_ += line_len + 2;

            std::vector<std::string> args;
            std::size_t start = 0;
            while (start < line.size())
            {
                while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start]))) ++start;
                if (start >= line.size()) break;
                std::size_t end = start;
                while (end < line.size() && !std::isspace(static_cast<unsigned char>(line[end]))) ++end;
                args.push_back(line.substr(start, end - start));
                start = end;
            }

            if (!args.empty())
            {
                Command cmd;
                cmd.args = std::move(args);
                commands.push_back(std::move(cmd));
            }
        }
    }

    if (read_offset_ > 0)
    {
        if (read_offset_ == buffer_.size())
        {
            buffer_.clear();
            read_offset_ = 0;
        }
        else if (read_offset_ > 65536)
        {
            buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(read_offset_));
            read_offset_ = 0;
        }
    }

    return true;
}

void Decoder::Reset()
{
    buffer_.clear();
    read_offset_ = 0;
}

}
