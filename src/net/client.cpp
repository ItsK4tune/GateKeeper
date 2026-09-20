#include "gatekeeper/net/client.h"
#include "gatekeeper/net/error.h"
#include "gatekeeper/protocol/gkwp2/binary_codec.h"
#include "gatekeeper/protocol/gkwp2/frame.h"
#include "gatekeeper/protocol/response.h"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <endian.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace gatekeeper::net
{

namespace
{

int ConnectWithTimeout(int socket_fd, const sockaddr* address, socklen_t address_length)
{
    const int flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags == -1 || fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        return errno;
    }
    int error = 0;
    if (connect(socket_fd, address, address_length) == -1)
    {
        error = errno;
        if (error == EINPROGRESS)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            pollfd descriptor{socket_fd, POLLOUT, 0};
            while (true)
            {
                const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now()).count();
                if (remaining <= 0)
                {
                    error = ETIMEDOUT;
                    break;
                }
                const int ready = poll(&descriptor, 1, static_cast<int>(remaining));
                if (ready == -1 && errno == EINTR)
                {
                    continue;
                }
                if (ready <= 0)
                {
                    error = ready == 0 ? ETIMEDOUT : errno;
                    break;
                }
                socklen_t size = sizeof(error);
                if (getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &error, &size) == -1)
                {
                    error = errno;
                }
                break;
            }
        }
    }
    if (fcntl(socket_fd, F_SETFL, flags) == -1 && error == 0)
    {
        error = errno;
    }
    return error;
}

std::string ExtractField(std::string_view json, std::string_view field)
{
    const auto search = "\"" + std::string(field) + "\":";
    auto pos = json.find(search);
    if (pos == std::string_view::npos) return "";
    pos += search.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    if (pos >= json.size()) return "";
    if (json[pos] == '"')
    {
        pos++;
        auto end = json.find('"', pos);
        if (end == std::string_view::npos) return "";
        return std::string(json.substr(pos, end - pos));
    }
    auto end = json.find_first_of(",}", pos);
    if (end == std::string_view::npos) end = json.size();
    return std::string(json.substr(pos, end - pos));
}

std::string DecodeBinaryResponseToText(protocol::gkwp2::BinaryOpcode op, std::string_view resp)
{
    if (resp.empty()) return "(error) empty response";
    const auto status = static_cast<protocol::gkwp2::BinaryStatus>(static_cast<std::uint8_t>(resp[0]));
    if (status == protocol::gkwp2::BinaryStatus::Error) return "(error) ERR_EXECUTION_FAILED";
    if (status == protocol::gkwp2::BinaryStatus::NotFound) return "(nil)";
    if (status == protocol::gkwp2::BinaryStatus::Conflict) return "(error) ERR_IDEMPOTENCY_CONFLICT";

    switch (op)
    {
    case protocol::gkwp2::BinaryOpcode::Ping:
        return "PONG";
    case protocol::gkwp2::BinaryOpcode::Set:
    case protocol::gkwp2::BinaryOpcode::QuotaInit:
        return "OK";
    case protocol::gkwp2::BinaryOpcode::Get:
    {
        if (resp.size() < 5) return "(nil)";
        std::uint32_t val_len;
        std::memcpy(&val_len, resp.data() + 1, 4);
        val_len = ntohl(val_len);
        if (resp.size() < 5 + val_len) return "(nil)";
        return protocol::QuoteJson(resp.substr(5, val_len));
    }
    case protocol::gkwp2::BinaryOpcode::Del:
    case protocol::gkwp2::BinaryOpcode::Exists:
    case protocol::gkwp2::BinaryOpcode::Expire:
    {
        if (resp.size() < 2) return "(integer) 0";
        return resp[1] ? "(integer) 1" : "(integer) 0";
    }
    case protocol::gkwp2::BinaryOpcode::Type:
    {
        if (resp.size() < 3) return "none";
        std::uint16_t tlen;
        std::memcpy(&tlen, resp.data() + 1, 2);
        tlen = ntohs(tlen);
        if (resp.size() < 3 + tlen) return "none";
        return std::string(resp.substr(3, tlen));
    }
    case protocol::gkwp2::BinaryOpcode::Ttl:
    {
        if (resp.size() < 9) return "(integer) -2";
        std::uint64_t ttl_net;
        std::memcpy(&ttl_net, resp.data() + 1, 8);
        auto ttl = static_cast<std::int64_t>(be64toh(ttl_net));
        return "(integer) " + std::to_string(ttl);
    }
    case protocol::gkwp2::BinaryOpcode::Incr:
    {
        if (resp.size() < 9) return "(integer) 0";
        std::uint64_t val_net;
        std::memcpy(&val_net, resp.data() + 1, 8);
        auto val = static_cast<std::int64_t>(be64toh(val_net));
        return "(integer) " + std::to_string(val);
    }
    case protocol::gkwp2::BinaryOpcode::QuotaGet:
    {
        if (resp.size() < 9) return "(integer) 0";
        std::uint64_t bal_net;
        std::memcpy(&bal_net, resp.data() + 1, 8);
        return "(integer) " + std::to_string(be64toh(bal_net));
    }
    case protocol::gkwp2::BinaryOpcode::RateLimit:
    {
        if (resp.size() < 18) return "(error) invalid rate limit response";
        bool allowed = resp[1] == 1;
        std::uint64_t rem_net, ret_net;
        std::memcpy(&rem_net, resp.data() + 2, 8);
        std::memcpy(&ret_net, resp.data() + 10, 8);
        return "allowed=" + std::string(allowed ? "1" : "0") +
               " remaining=" + std::to_string(be64toh(rem_net)) +
               " retry_after_ms=" + std::to_string(be64toh(ret_net));
    }
    case protocol::gkwp2::BinaryOpcode::Reserve:
    {
        if (resp.size() < 12) return "(error) invalid reserve response";
        bool granted = resp[1] == 1;
        std::uint64_t rem_net;
        std::memcpy(&rem_net, resp.data() + 2, 8);
        std::uint16_t rlen;
        std::memcpy(&rlen, resp.data() + 10, 2);
        rlen = ntohs(rlen);
        std::string res_id;
        if (resp.size() >= 12 + rlen) res_id = resp.substr(12, rlen);
        return "reserved=" + std::string(granted ? "1" : "0") +
               " reservation_id=" + protocol::QuoteJson(res_id) +
               " remaining=" + std::to_string(be64toh(rem_net));
    }
    case protocol::gkwp2::BinaryOpcode::Commit:
    {
        if (resp.size() < 18) return "(error) invalid commit response";
        std::uint64_t ref_net, bal_net;
        std::memcpy(&ref_net, resp.data() + 2, 8);
        std::memcpy(&bal_net, resp.data() + 10, 8);
        return "committed=1 refunded=" + std::to_string(be64toh(ref_net)) +
               " remaining=" + std::to_string(be64toh(bal_net));
    }
    case protocol::gkwp2::BinaryOpcode::Rollback:
    {
        if (resp.size() < 18) return "(error) invalid rollback response";
        std::uint64_t ref_net, bal_net;
        std::memcpy(&ref_net, resp.data() + 2, 8);
        std::memcpy(&bal_net, resp.data() + 10, 8);
        return "rolled_back=1 refunded=" + std::to_string(be64toh(ref_net)) +
               " remaining=" + std::to_string(be64toh(bal_net));
    }
    case protocol::gkwp2::BinaryOpcode::IdemBegin:
    {
        if (resp.size() < 4) return "(error) invalid idem begin response";
        std::uint8_t action = resp[1];
        std::string act_str = action == 0 ? "EXECUTE" : (action == 1 ? "PARK" : (action == 2 ? "REPLAY" : "CONFLICT"));
        std::uint16_t olen;
        std::memcpy(&olen, resp.data() + 2, 2);
        olen = ntohs(olen);
        std::string token;
        if (resp.size() >= 4 + olen) token = resp.substr(4, olen);
        return "action=" + act_str + " owner_token=" + protocol::QuoteJson(token);
    }
    case protocol::gkwp2::BinaryOpcode::IdemComplete:
        return "completed=1";
    case protocol::gkwp2::BinaryOpcode::IdemFail:
        return "failed=1";
    default:
        return "OK";
    }
}

}

Client::Client(std::string host, std::uint16_t port, std::shared_ptr<log::Logger> logger)
    : host_(std::move(host)), port_(port), logger_(std::move(logger))
{
    if (!logger_)
    {
        logger_ = log::Logger::Null();
    }
    if (host_.empty())
    {
        throw std::invalid_argument("INVALID_HOST: host cannot be empty");
    }
    if (port_ == 0)
    {
        throw std::invalid_argument("INVALID_PORT: port must be between 1 and 65535");
    }
}

Client::~Client()
{
    Close();
}

void Client::Close()
{
    if (socket_fd_ != -1)
    {
        logger_->Info("Closing connection to " + host_ + ':' + std::to_string(port_));
        close(socket_fd_);
        socket_fd_ = -1;
    }
}

void Client::Open()
{
    if (socket_fd_ != -1)
    {
        return;
    }
    const auto service = std::to_string(port_);
    logger_->Info("Connecting to " + host_ + ':' + service + "...");
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    addrinfo* addresses = nullptr;
    const int status = getaddrinfo(host_.c_str(), service.c_str(), &hints, &addresses);
    if (status != 0)
    {
        const auto err_msg = "DNS_ERROR: cannot resolve " + host_ + ':' + service + ": " +
                             gai_strerror(status) + ". Check the host name or use an IP address.";
        logger_->Error(err_msg);
        throw std::runtime_error(err_msg);
    }
    int last_error = ECONNREFUSED;
    for (auto* address = addresses; address != nullptr; address = address->ai_next)
    {
        socket_fd_ = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (socket_fd_ == -1)
        {
            last_error = errno;
            continue;
        }
        last_error = ConnectWithTimeout(socket_fd_, address->ai_addr, address->ai_addrlen);
        if (last_error == 0)
        {
            break;
        }
        Close();
    }
    freeaddrinfo(addresses);
    if (socket_fd_ == -1)
    {
        const auto err_msg = DescribeError("connect to", host_ + ':' + service, last_error);
        logger_->Error(err_msg);
        throw std::runtime_error(err_msg);
    }
    const timeval timeout{5, 0};
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1 ||
        setsockopt(socket_fd_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == -1)
    {
        const int error = errno;
        Close();
        const auto err_msg = DescribeError("set timeout for", host_ + ':' + service, error);
        logger_->Error(err_msg);
        throw std::runtime_error(err_msg);
    }
    logger_->Info("Connected to " + host_ + ':' + service);
}

void Client::ReadAll(std::span<std::uint8_t> bytes)
{
    while (!bytes.empty())
    {
        const auto received = recv(socket_fd_, bytes.data(), bytes.size(), 0);
        if (received == -1 && errno == EINTR)
        {
            continue;
        }
        if (received == 0)
        {
            const auto err_msg = "CONNECTION_CLOSED: " + host_ + ':' + std::to_string(port_) +
                                 " closed before the complete response was received.";
            logger_->Error(err_msg);
            throw std::runtime_error(err_msg);
        }
        if (received < 0)
        {
            const auto err_msg = DescribeError("read from", host_ + ':' + std::to_string(port_), errno);
            logger_->Error(err_msg);
            throw std::runtime_error(err_msg);
        }
        bytes = bytes.subspan(static_cast<std::size_t>(received));
    }
}

void Client::SendAll(std::span<const std::uint8_t> bytes)
{
    while (!bytes.empty())
    {
        const auto written = send(socket_fd_, bytes.data(), bytes.size(), MSG_NOSIGNAL);
        if (written == -1 && errno == EINTR)
        {
            continue;
        }
        if (written <= 0)
        {
            const auto err_msg = DescribeError("write to", host_ + ':' + std::to_string(port_),
                                               written == 0 ? EPIPE : errno);
            logger_->Error(err_msg);
            throw std::runtime_error(err_msg);
        }
        bytes = bytes.subspan(static_cast<std::size_t>(written));
    }
}

std::string Client::ExecuteBinary(std::span<const std::uint8_t> binary_payload)
{
    Open();
    try
    {
        const auto req_num = next_id_++;
        protocol::gkwp2::Header hdr{};
        hdr.magic = protocol::gkwp2::kMagic;
        hdr.version = protocol::gkwp2::kVersion;
        hdr.flags = protocol::gkwp2::flags::kEndStream | protocol::gkwp2::flags::kBinaryPayload;
        hdr.msg_type = static_cast<std::uint8_t>(protocol::gkwp2::MsgType::Request);
        hdr.request_id = req_num;
        hdr.stream_id = 1;
        hdr.payload_len = static_cast<std::uint32_t>(binary_payload.size());

        std::string_view payload_view(reinterpret_cast<const char*>(binary_payload.data()), binary_payload.size());
        SendAll(protocol::gkwp2::EncodeFrame(hdr, payload_view));

        std::array<std::uint8_t, protocol::gkwp2::kHeaderSize> header_buf{};
        ReadAll(header_buf);

        const auto magic = (static_cast<std::uint16_t>(header_buf[0]) << 8U) | header_buf[1];
        if (magic != protocol::gkwp2::kMagic)
        {
            throw std::runtime_error("INVALID_MAGIC: expected GKWP/2 magic from " + host_ + ':' + std::to_string(port_));
        }

        const auto length = (static_cast<std::uint32_t>(header_buf[20]) << 24U) |
                            (static_cast<std::uint32_t>(header_buf[21]) << 16U) |
                            (static_cast<std::uint32_t>(header_buf[22]) << 8U) | header_buf[23];

        if (length > protocol::gkwp2::kMaxPayloadLength)
        {
            const auto err_msg = "INVALID_FRAME: response from " + host_ + ':' + std::to_string(port_) +
                                 " declares " + std::to_string(length) + " bytes; exceeds limit.";
            logger_->Error(err_msg);
            throw std::runtime_error(err_msg);
        }

        std::vector<std::uint8_t> response_bytes(length);
        if (length > 0)
        {
            ReadAll(response_bytes);
        }
        return std::string(reinterpret_cast<const char*>(response_bytes.data()), response_bytes.size());
    }
    catch (...)
    {
        Close();
        throw;
    }
}

std::string Client::Execute(const std::string& operation, const std::string& body)
{
    std::string bin_req;
    protocol::gkwp2::BinaryOpcode op_code = protocol::gkwp2::BinaryOpcode::Ping;

    if (operation == "PING")
    {
        op_code = protocol::gkwp2::BinaryOpcode::Ping;
        bin_req = protocol::gkwp2::BinaryCodec::EncodePing();
    }
    else if (operation == "GET")
    {
        op_code = protocol::gkwp2::BinaryOpcode::Get;
        const auto key = ExtractField(body, "key");
        bin_req = protocol::gkwp2::BinaryCodec::EncodeGet(key);
    }
    else if (operation == "SET")
    {
        op_code = protocol::gkwp2::BinaryOpcode::Set;
        const auto key = ExtractField(body, "key");
        const auto val = ExtractField(body, "value");
        const auto ttl_str = ExtractField(body, "ttl_ms");
        std::uint64_t ttl = ttl_str.empty() ? 0 : std::stoull(ttl_str);
        auto cond = protocol::gkwp2::SetCondition::Always;
        if (body.find("\"if_not_exists\":true") != std::string::npos) cond = protocol::gkwp2::SetCondition::IfAbsent;
        else if (body.find("\"if_exists\":true") != std::string::npos) cond = protocol::gkwp2::SetCondition::IfPresent;
        bin_req = protocol::gkwp2::BinaryCodec::EncodeSet(key, val, cond, ttl);
    }
    else if (operation == "DEL")
    {
        op_code = protocol::gkwp2::BinaryOpcode::Del;
        const auto key = ExtractField(body, "key");
        bin_req = protocol::gkwp2::BinaryCodec::EncodeDel(key);
    }
    else if (operation == "EXISTS")
    {
        op_code = protocol::gkwp2::BinaryOpcode::Exists;
        const auto key = ExtractField(body, "key");
        bin_req = protocol::gkwp2::BinaryCodec::EncodeExists(key);
    }
    else if (operation == "TYPE")
    {
        op_code = protocol::gkwp2::BinaryOpcode::Type;
        const auto key = ExtractField(body, "key");
        bin_req = protocol::gkwp2::BinaryCodec::EncodeType(key);
    }
    else if (operation == "EXPIRE" || operation == "PEXPIRE")
    {
        op_code = protocol::gkwp2::BinaryOpcode::Expire;
        const auto key = ExtractField(body, "key");
        const auto ttl_str = ExtractField(body, "ttl_ms");
        std::uint64_t ttl = ttl_str.empty() ? 0 : std::stoull(ttl_str);
        bin_req = protocol::gkwp2::BinaryCodec::EncodeExpire(key, ttl);
    }
    else if (operation == "TTL" || operation == "PTTL")
    {
        op_code = protocol::gkwp2::BinaryOpcode::Ttl;
        const auto key = ExtractField(body, "key");
        bin_req = protocol::gkwp2::BinaryCodec::EncodeTtl(key);
    }
    else if (operation == "INCR")
    {
        op_code = protocol::gkwp2::BinaryOpcode::Incr;
        const auto key = ExtractField(body, "key");
        bin_req = protocol::gkwp2::BinaryCodec::EncodeIncrBy(key, 1, 0);
    }
    else if (operation == "DECR")
    {
        op_code = protocol::gkwp2::BinaryOpcode::Incr;
        const auto key = ExtractField(body, "key");
        bin_req = protocol::gkwp2::BinaryCodec::EncodeIncrBy(key, -1, 0);
    }
    else if (operation == "INCRBY")
    {
        op_code = protocol::gkwp2::BinaryOpcode::Incr;
        const auto key = ExtractField(body, "key");
        const auto delta_str = ExtractField(body, "delta");
        std::int64_t delta = delta_str.empty() ? 1 : std::stoll(delta_str);
        const auto ttl_str = ExtractField(body, "ttl_ms");
        std::uint64_t ttl = ttl_str.empty() ? 0 : std::stoull(ttl_str);
        bin_req = protocol::gkwp2::BinaryCodec::EncodeIncrBy(key, delta, ttl);
    }
    else if (operation == "GK.RATE_LIMIT")
    {
        op_code = protocol::gkwp2::BinaryOpcode::RateLimit;
        const auto key = ExtractField(body, "key");
        const auto limit = std::stoull(ExtractField(body, "limit"));
        const auto win = std::stoull(ExtractField(body, "window_ms"));
        bin_req = protocol::gkwp2::BinaryCodec::EncodeRateLimit(key, limit, win, 1);
    }
    else if (operation == "GK.QUOTA_INIT")
    {
        op_code = protocol::gkwp2::BinaryOpcode::QuotaInit;
        const auto key = ExtractField(body, "key");
        const auto amt = std::stoull(ExtractField(body, "amount"));
        bin_req = protocol::gkwp2::BinaryCodec::EncodeQuotaInit(key, amt);
    }
    else if (operation == "GK.QUOTA_GET")
    {
        op_code = protocol::gkwp2::BinaryOpcode::QuotaGet;
        const auto key = ExtractField(body, "key");
        bin_req = protocol::gkwp2::BinaryCodec::EncodeQuotaGet(key);
    }
    else if (operation == "GK.RESERVE")
    {
        op_code = protocol::gkwp2::BinaryOpcode::Reserve;
        const auto key = ExtractField(body, "key");
        const auto amt = std::stoull(ExtractField(body, "amount"));
        const auto ttl = std::stoull(ExtractField(body, "ttl_ms"));
        bin_req = protocol::gkwp2::BinaryCodec::EncodeReserve(key, amt, ttl);
    }
    else if (operation == "GK.COMMIT")
    {
        op_code = protocol::gkwp2::BinaryOpcode::Commit;
        const auto key = ExtractField(body, "key");
        const auto res_id = ExtractField(body, "reservation_id");
        const auto act = std::stoull(ExtractField(body, "actual_amount"));
        bin_req = protocol::gkwp2::BinaryCodec::EncodeCommit(key, res_id, act);
    }
    else if (operation == "GK.ROLLBACK")
    {
        op_code = protocol::gkwp2::BinaryOpcode::Rollback;
        const auto key = ExtractField(body, "key");
        const auto res_id = ExtractField(body, "reservation_id");
        bin_req = protocol::gkwp2::BinaryCodec::EncodeRollback(key, res_id);
    }
    else if (operation == "GK.IDEM_BEGIN")
    {
        op_code = protocol::gkwp2::BinaryOpcode::IdemBegin;
        const auto key = ExtractField(body, "key");
        const auto hash = ExtractField(body, "request_hash");
        const auto ttl_str = ExtractField(body, "ttl_ms");
        std::uint64_t ttl = ttl_str.empty() ? 0 : std::stoull(ttl_str);
        const auto owner = ExtractField(body, "owner_token");
        bin_req = protocol::gkwp2::BinaryCodec::EncodeIdemBegin(key, hash, ttl, owner);
    }
    else if (operation == "GK.IDEM_COMPLETE")
    {
        op_code = protocol::gkwp2::BinaryOpcode::IdemComplete;
        const auto key = ExtractField(body, "key");
        const auto owner = ExtractField(body, "owner_token");
        const auto code = std::stoi(ExtractField(body, "response_code"));
        const auto resp_b = ExtractField(body, "response_body");
        bin_req = protocol::gkwp2::BinaryCodec::EncodeIdemComplete(key, owner, code, resp_b);
    }
    else if (operation == "GK.IDEM_FAIL")
    {
        op_code = protocol::gkwp2::BinaryOpcode::IdemFail;
        const auto key = ExtractField(body, "key");
        const auto owner = ExtractField(body, "owner_token");
        const auto msg = ExtractField(body, "error_message");
        bin_req = protocol::gkwp2::BinaryCodec::EncodeIdemFail(key, owner, msg);
    }
    else
    {
        throw std::invalid_argument("unsupported operation for binary protocol: " + operation);
    }

    const auto raw_resp = ExecuteBinary(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bin_req.data()), bin_req.size()));
    return DecodeBinaryResponseToText(op_code, raw_resp);
}

}
