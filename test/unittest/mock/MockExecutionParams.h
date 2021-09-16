#pragma once

#include "bcos-framework/interfaces/executor/ExecutionParams.h"
#include "interfaces/crypto/CommonType.h"
#include "libutilities/Common.h"

namespace bcos::test
{
class MockExecutionParams : public protocol::ExecutionParams
{
public:
    virtual ~MockExecutionParams() {}

    Type type() const override { return m_type; }
    void setType(Type type) override { m_type = type; }

    int64_t contextID() const override { return m_contextID; }
    void setContextID(int64_t contextID) override { m_contextID = contextID; }

    int64_t gasAvailable() const override { return m_gasAvailable; }
    void setGasAvailable(int64_t gasAvailable) override { m_gasAvailable = gasAvailable; }

    bcos::bytesConstRef input() const override { return ref(m_input); }
    void setInput(bcos::bytes input) override { m_input = std::move(input); }

    std::string_view origin() const override { return m_origin; }
    void setOrigin(std::string origin) override { m_origin = std::move(origin); }

    std::string_view from() const override { return m_from; }
    void setFrom(std::string from) override { m_from = std::move(from); }

    crypto::HashType transactionHash() const override { return m_transactionHash; }
    void setTransactionHash(crypto::HashType hash) override { m_transactionHash = hash; }

    std::string_view to() const override { return m_to; }
    void setTo(std::string to) override { m_to = std::move(to); }

    int32_t status() const override { return m_status; }
    void setStatus(int32_t status) override { m_status = status; }

    std::string_view message() const override { return m_message; }
    void setMessage(std::string message) override { m_message = std::move(message); }

    int32_t depth() const override { return m_depth; }
    void setDepth(int32_t depth) override { m_depth = depth; }

    std::optional<u256> createSalt() const override { return m_createSalt; }
    void setCreateSalt(u256 createSalt) override { m_createSalt = createSalt; }

    bool staticCall() const override { return m_staticCall; }
    void setStaticCall(bool staticCall) override { m_staticCall = staticCall; }

    Type m_type;
    int64_t m_contextID;
    int64_t m_gasAvailable;
    bcos::bytes m_input;
    std::string m_origin;
    std::string m_from;
    bcos::crypto::HashType m_transactionHash;
    std::string m_to;
    int32_t m_status;
    std::string m_message;
    int32_t m_depth;
    std::optional<u256> m_createSalt;
    bool m_staticCall;
};

class MockExecutionParamsFactory : public protocol::ExecutionParamsFactory
{
public:
    protocol::ExecutionParams::Ptr createExecutionParams() override
    {
        return std::make_shared<MockExecutionParams>();
    }
};
}  // namespace bcos::test