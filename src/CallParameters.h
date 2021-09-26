#pragma once

#include "bcos-framework/libprotocol/LogEntry.h"
#include "bcos-framework/libutilities/Common.h"
#include <memory>
#include <string>

namespace bcos::executor
{
struct CallParameters
{
    using UniquePtr = std::unique_ptr<CallParameters>;
    using UniqueConstPtr = std::unique_ptr<const CallParameters>;

    enum Type : int8_t
    {
        MESSAGE = 0,
        FINISHED,
        REVERT,
    };

    explicit CallParameters(Type _type) : type(_type) {}

    CallParameters(const CallParameters&) = delete;
    CallParameters& operator=(const CallParameters&) = delete;

    CallParameters(CallParameters&&) = delete;
    CallParameters(const CallParameters&&) = delete;

    Type type;
    std::string senderAddress;   // by request or response, readable format
    std::string codeAddress;     // by request or response, readable format
    std::string receiveAddress;  // by request or response, readable format
    std::string origin;          // by request or response, readable format

    int64_t gas;       // by request or response
    bcos::bytes data;  // by request or response, transaction data, binary format
    bool staticCall;   // by request or response
    bool create;       // by request, is create

    int32_t status;                                    // by response
    std::string message;                               // by response, readable format
    std::vector<bcos::protocol::LogEntry> logEntries;  // by response
    std::optional<u256> createSalt;                    // by response
    std::string newEVMContractAddress;                 // by response, readable format
};
}  // namespace bcos::executor