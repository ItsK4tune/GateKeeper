#include "gatekeeper/core/log/logger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace gatekeeper::log
{
namespace
{

std::string CurrentTimestamp()
{
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    const auto timer = std::chrono::system_clock::to_time_t(now);
    std::tm bt{};
#if defined(_WIN32)
    localtime_s(&bt, &timer);
#else
    localtime_r(&timer, &bt);
#endif
    std::ostringstream ss;
    ss << std::put_time(&bt, "%Y-%m-%d %H:%M:%S")
       << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

}

Logger::Logger(std::unique_ptr<Sink> sink, Level min_level)
    : sink_(std::move(sink)), min_level_(min_level)
{
    if (!sink_)
    {
        sink_ = std::make_unique<NullSink>();
    }
}

void Logger::Log(Level level, std::string_view message)
{
    if (!IsEnabled(level))
    {
        return;
    }
    const auto timestamp = CurrentTimestamp();
    const auto level_str = ToString(level);
    std::string formatted;
    formatted.reserve(timestamp.size() + level_str.size() + message.size() + 7);
    formatted += '[';
    formatted += timestamp;
    formatted += "] [";
    formatted += level_str;
    formatted += "] ";
    formatted += message;
    formatted += '\n';

    const std::lock_guard lock(mutex_);
    sink_->Write(formatted);
    sink_->Flush();
}

std::shared_ptr<Logger> Logger::Create(Mode mode, const std::string& directory)
{
    switch (mode)
    {
    case Mode::None:
        return std::make_shared<Logger>(std::make_unique<NullSink>(), Level::None);
    case Mode::Terminal:
        return std::make_shared<Logger>(std::make_unique<ConsoleSink>(), Level::Debug);
    case Mode::File:
    {
        std::filesystem::path dir = directory.empty() ? "./logs" : directory;
        return std::make_shared<Logger>(std::make_unique<FileSink>(dir / "log"), Level::Debug);
    }
    }
    return Null();
}

std::shared_ptr<Logger> Logger::Null()
{
    static const auto instance = std::make_shared<Logger>(std::make_unique<NullSink>(), Level::None);
    return instance;
}

}
