#include "gatekeeper/storage/aof/aof_writer.h"

#include <cerrno>
#include <fcntl.h>
#include <filesystem>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace gatekeeper::storage::aof
{

AofWriter::AofWriter(std::string file_path, FsyncPolicy policy)
    : file_path_(std::move(file_path)), policy_(policy)
{
    const auto parent = std::filesystem::path(file_path_).parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent))
    {
        std::filesystem::create_directories(parent);
    }
    fd_ = open(file_path_.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
}

AofWriter::~AofWriter()
{
    Close();
}

bool AofWriter::Append(std::string_view op, std::string_view payload_json)
{
    if (op.empty())
    {
        return false;
    }

    const std::lock_guard lock(mutex_);
    if (fd_ == -1)
    {
        return false;
    }

    std::string line;
    line.reserve(op.size() + 1 + payload_json.size() + 1);
    line.append(op);
    line.push_back(' ');
    line.append(payload_json);
    line.push_back('\n');

    const char* ptr = line.data();
    std::size_t remaining = line.size();
    while (remaining > 0)
    {
        const auto written = write(fd_, ptr, remaining);
        if (written <= 0)
        {
            if (written == -1 && errno == EINTR)
            {
                continue;
            }
            return false;
        }
        ptr += written;
        remaining -= static_cast<std::size_t>(written);
    }

    if (policy_ == FsyncPolicy::Always)
    {
        fdatasync(fd_);
    }

    return true;
}

void AofWriter::Fsync()
{
    const std::lock_guard lock(mutex_);
    if (fd_ != -1)
    {
        fdatasync(fd_);
    }
}

void AofWriter::Close()
{
    const std::lock_guard lock(mutex_);
    if (fd_ != -1)
    {
        fdatasync(fd_);
        close(fd_);
        fd_ = -1;
    }
}

bool AofWriter::IsOpen() const noexcept
{
    const std::lock_guard lock(mutex_);
    return fd_ != -1;
}

}
