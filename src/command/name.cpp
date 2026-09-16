#include "gatekeeper/command/name.h"

namespace gatekeeper::command
{

std::string NormalizeName(std::string_view name)
{
    std::string normalized(name);
    for (char& character : normalized)
    {
        if (character >= 'a' && character <= 'z')
        {
            character = static_cast<char>(character - 'a' + 'A');
        }
    }
    return normalized;
}

}
