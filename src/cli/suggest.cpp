#include "gatekeeper/cli/suggest.h"

#include <algorithm>
#include <utility>

namespace gatekeeper::cli
{
namespace
{

std::size_t EditDistance(std::string_view first, std::string_view second)
{
    std::vector<std::vector<std::size_t>> distance(first.size() + 1,
                                                  std::vector<std::size_t>(second.size() + 1));
    for (std::size_t i = 0; i <= first.size(); ++i)
    {
        distance[i][0] = i;
    }
    for (std::size_t j = 0; j <= second.size(); ++j)
    {
        distance[0][j] = j;
    }
    for (std::size_t i = 1; i <= first.size(); ++i)
    {
        for (std::size_t j = 1; j <= second.size(); ++j)
        {
            distance[i][j] = std::min({distance[i - 1][j] + 1, distance[i][j - 1] + 1,
                                     distance[i - 1][j - 1] + (first[i - 1] != second[j - 1])});
            if (i > 1 && j > 1 && first[i - 1] == second[j - 2] && first[i - 2] == second[j - 1])
            {
                distance[i][j] = std::min(distance[i][j], distance[i - 2][j - 2] + 1);
            }
        }
    }
    return distance.back().back();
}

}

std::vector<std::string> Suggest(std::string_view input, const std::vector<std::string>& names)
{
    if (input.empty() || input.size() > 64)
    {
        return {};
    }
    const std::size_t threshold = input.size() <= 2 ? 1 : 2;
    std::vector<std::pair<std::size_t, std::string>> matches;
    for (const auto& name : names)
    {
        if (name.size() > 64 || name.size() > input.size() + threshold || input.size() > name.size() + threshold)
        {
            continue;
        }
        const auto distance = EditDistance(input, name);
        if (distance <= threshold)
        {
            matches.emplace_back(distance, name);
        }
    }
    std::sort(matches.begin(), matches.end());
    std::vector<std::string> suggestions;
    for (std::size_t i = 0; i < std::min<std::size_t>(3, matches.size()); ++i)
    {
        suggestions.push_back(matches[i].second);
    }
    return suggestions;
}

}
