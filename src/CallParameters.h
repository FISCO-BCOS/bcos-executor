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
    std::string senderAddress;   // common field, readable format
    std::string codeAddress;     // common field, readable format
    std::string receiveAddress;  // common field, readable format
    std::string origin;          // common field, readable format

    int64_t gas;       // common field
    bcos::bytes data;  // common field, transaction data, binary format
    bool staticCall;   // common field
    bool create;       // by request, is create

    int32_t status;                                    // by response
    std::string message;                               // by response, readable format
    std::vector<bcos::protocol::LogEntry> logEntries;  // by response
    std::optional<u256> createSalt;                    // by response
    std::string newEVMContractAddress;                 // by response, readable format
};
}  // namespace bcos::executor