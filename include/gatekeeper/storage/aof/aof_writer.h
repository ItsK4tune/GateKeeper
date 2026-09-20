#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

namespace gatekeeper::storage::aof
{

enum class FsyncPolicy
{
    Always,
    Everysec,
    No
};

class AofWriter
{
public:
    AofWriter(std::string file_path, FsyncPolicy policy);
    ~AofWriter();

    AofWriter(const AofWriter&) = delete;
    AofWriter& operator=(const AofWriter&) = delete;

    bool Append(std::string_view op, std::string_view payload_json);
    void Fsync();
    void Close();

    [[nodiscard]] const std::string& FilePath() const noexcept { return file_path_; }
    [[nodiscard]] FsyncPolicy Policy() const noexcept { return policy_; }
    [[nodiscard]] bool IsOpen() const noexcept;

private:
    std::string file_path_;
    FsyncPolicy policy_;
    mutable std::mutex mutex_;
    int fd_{-1};
};

}
