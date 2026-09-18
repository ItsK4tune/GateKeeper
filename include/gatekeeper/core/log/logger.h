#pragma once

#include "gatekeeper/core/log/level.h"
#include "gatekeeper/core/log/sink.h"

#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace gatekeeper::log
{

enum class Mode
{
    None,
    Terminal,
    File
};

class Logger
{
public:
    explicit Logger(std::unique_ptr<Sink> sink, Level min_level = Level::Debug);
    ~Logger() = default;

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void Log(Level level, std::string_view message);
    void Debug(std::string_view message) { Log(Level::Debug, message); }
    void Info(std::string_view message)  { Log(Level::Info, message); }
    void Warn(std::string_view message)  { Log(Level::Warn, message); }
    void Error(std::string_view message) { Log(Level::Error, message); }

    bool IsEnabled(Level level) const noexcept
    {
        return min_level_ != Level::None && level >= min_level_;
    }

    static std::shared_ptr<Logger> Create(Mode mode, const std::string& directory = "");
    static std::shared_ptr<Logger> Null();

private:
    std::unique_ptr<Sink> sink_;
    Level min_level_;
    mutable std::mutex mutex_;
};

}
