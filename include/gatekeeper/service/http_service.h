#pragma once

#include "gatekeeper/log/logger.h"
#include "gatekeeper/net/http_types.h"
#include "gatekeeper/storage/store.h"

#include <memory>
#include <string>

namespace gatekeeper::service
{

class HttpService
{
public:
    explicit HttpService(storage::Store& store, std::shared_ptr<log::Logger> logger = log::Logger::Null());

    net::HttpResponse Handle(const net::HttpRequest& req);

private:
    storage::Store& store_;
    std::shared_ptr<log::Logger> logger_;

    net::HttpResponse HandleHealthz(const net::HttpRequest& req);
    net::HttpResponse HandleRateLimitCheck(const net::HttpRequest& req);
    net::HttpResponse HandleQuotaReserve(const net::HttpRequest& req);
    net::HttpResponse HandleQuotaCommit(const net::HttpRequest& req);
    net::HttpResponse HandleQuotaRollback(const net::HttpRequest& req);
    net::HttpResponse HandleQuotaInit(const net::HttpRequest& req);

    static net::HttpResponse BadRequest(const std::string& message);
};

}
