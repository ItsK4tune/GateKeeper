#include "gatekeeper/storage/aof/aof_loader.h"

#include <filesystem>
#include <fstream>

namespace gatekeeper::storage::aof
{

LoadResult AofLoader::Load(std::string_view file_path, command::Dispatcher& dispatcher)
{
    if (!std::filesystem::exists(file_path))
    {
        return {true, 0, 0, {}};
    }

    const std::string path_str(file_path);
    std::ifstream file(path_str);
    if (!file.is_open())
    {
        return {false, 0, 0, "failed to open AOF file for reading"};
    }

    LoadResult result;
    std::string line;
    while (std::getline(file, line))
    {
        if (line.empty() || line.front() == '#')
        {
            continue;
        }

        const auto space_pos = line.find(' ');
        if (space_pos == std::string::npos)
        {
            result.lines_skipped++;
            continue;
        }

        const auto op = line.substr(0, space_pos);
        const auto body_json = line.substr(space_pos + 1);

        auto dispatch_res = dispatcher.Dispatch(protocol::Request{"aof", op, body_json});
        if (dispatch_res.ok)
        {
            result.lines_replayed++;
        }
        else
        {
            result.lines_skipped++;
        }
    }

    return result;
}

}
