#include "gatekeeper/storage/aof/aof_writer.h"

#include <filesystem>

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
    file_.open(file_path_, std::ios::out | std::ios::app);
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
    if (!file_.is_open())
    {
        return false;
    }

    file_ << op << " " << payload_json << "\n";
    file_.flush();

    return true;
}

void AofWriter::Fsync()
{
    const std::lock_guard lock(mutex_);
    if (file_.is_open())
    {
        file_.flush();
    }
}

void AofWriter::Close()
{
    const std::lock_guard lock(mutex_);
    if (file_.is_open())
    {
        file_.flush();
        file_.close();
    }
}

bool AofWriter::IsOpen() const noexcept
{
    const std::lock_guard lock(mutex_);
    return file_.is_open();
}

}
