#pragma once

#include "gatekeeper/command/dispatcher.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace gatekeeper::storage::aof
{

struct LoadResult
{
    bool ok{true};
    std::size_t lines_replayed{0};
    std::size_t lines_skipped{0};
    std::string error_message;
};

class AofLoader
{
public:
    static LoadResult Load(std::string_view file_path, command::Dispatcher& dispatcher);
};

}
