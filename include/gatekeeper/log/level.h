#pragma once

#include <string_view>

namespace gatekeeper::log
{

enum class Level
{
    Debug,
    Info,
    Warn,
    Error,
    None
};

inline std::string_view ToString(Level level) noexcept
{
    switch (level)
    {
    case Level::Debug: return "DEBUG";
    case Level::Info:  return "INFO";
    case Level::Warn:  return "WARN";
    case Level::Error: return "ERROR";
    case Level::None:  return "NONE";
    }
    return "UNKNOWN";
}

}
