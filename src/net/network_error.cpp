#include "gatekeeper/net/network_error.h"

#include <cerrno>
#include <system_error>

namespace gatekeeper::net
{

std::string DescribeNetworkError(const std::string& operation, const std::string& endpoint, int error)
{
    std::string code = "NETWORK_ERROR";
    std::string hint;
    switch (error)
    {
    case EADDRINUSE:
        code = "ADDRESS_IN_USE";
        hint = "Another process is using this address/port. Check it with ss -ltnp.";
        break;
    case ECONNREFUSED:
        code = "CONNECTION_REFUSED";
        hint = "Check that gatekeeper is running and the host/port are correct.";
        break;
    case EACCES:
    case EPERM:
        code = "PERMISSION_DENIED";
        hint = "Check socket permissions and firewall rules.";
        break;
    case ETIMEDOUT:
    case EAGAIN:
        code = "TIMEOUT";
        hint = "The operation timed out. Check the server and network.";
        break;
    case ENETUNREACH:
    case EHOSTUNREACH:
        code = "UNREACHABLE";
        hint = "Check the host address, network route and firewall.";
        break;
    case ECONNRESET:
    case EPIPE:
        code = "CONNECTION_LOST";
        hint = "The peer closed or reset the connection.";
        break;
    case EADDRNOTAVAIL:
        code = "ADDRESS_UNAVAILABLE";
        hint = "Check that the address belongs to this machine.";
        break;
    default:
        break;
    }
    std::string message = code + ": " + operation + " " + endpoint + ": " +
                          std::error_code(error, std::generic_category()).message();
    if (!hint.empty())
    {
        message += ". " + hint;
    }
    return message;
}

}
