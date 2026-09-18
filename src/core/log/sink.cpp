#include "gatekeeper/core/log/sink.h"
#include <system_error>

namespace gatekeeper::log
{

FileSink::FileSink(const std::filesystem::path& file_path)
{
    const auto parent = file_path.parent_path();
    if (!parent.empty())
    {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
    }
    stream_.open(file_path, std::ios::out | std::ios::app);
}

void FileSink::Write(std::string_view message)
{
    if (stream_.is_open())
    {
        stream_ << message;
    }
}

void FileSink::Flush()
{
    if (stream_.is_open())
    {
        stream_.flush();
    }
}

}
