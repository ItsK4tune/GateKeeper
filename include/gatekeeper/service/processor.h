#pragma once

#include "gatekeeper/core/log/logger.h"
#include "gatekeeper/protocol/gkwp2/binary_codec.h"
#include "gatekeeper/storage/aof/aof_writer.h"
#include "gatekeeper/storage/store.h"

#include <memory>
#include <string>
#include <string_view>

namespace gatekeeper::service
{

class Processor
{
public:
    explicit Processor(storage::Store* store,
                       std::shared_ptr<log::Logger> logger = log::Logger::Null(),
                       std::shared_ptr<storage::aof::AofWriter> aof_writer = nullptr);

    std::string Process(std::string_view payload) const;

private:
    storage::Store* store_{nullptr};
    std::shared_ptr<log::Logger> logger_;
    std::shared_ptr<storage::aof::AofWriter> aof_writer_;
};

}
