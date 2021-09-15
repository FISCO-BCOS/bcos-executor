#pragma once

#include "bcos-framework/interfaces/executor/ExecutionResult.h"

namespace bcos::test
{
class MockExecutionResult : public bcos::protocol::ExecutionResult
{
public:
    Type type() const override { return m_type; }
    void setType(Type type) override { m_type = type; }

    int64_t contextID() const override { return m_contextID; }
    void setContextID(int64_t contextID) override { m_contextID = contextID; }

    int64_t gasAvailable() const override { return m_gasAvailable; }
    void setGasAvailable(int64_t gasAvailable) override { m_gasAvailable = gasAvailable; }

    int32_t status() const override { return m_status; }
    void setStatus(int32_t status) override { m_status = status; }

    std::string_view message() const override { return m_message; }
    void setMessage(std::string message) override { m_message = std::move(message); }

    bcos::bytesConstRef output() const override { return ref(m_output); }
    void setOutput(bytes output) override { m_output = std::move(output); }

    gsl::span<bcos::protocol::LogEntry> logEntries() const override { return *m_logEntries; }
    void setLogEntries(bcos::protocol::LogEntriesPtr logEntries) override
    {
        m_logEntries = std::move(logEntries);
    }

    std::string_view to() const override { return m_to; }
    void setTo(std::string to) override { m_to = std::move(to); }

    std::string_view newEVMContractAddress() const override { return m_newEVMContractAddress; }
    void setNewEVMContractAddress(std::string newEVMContractAddress) override
    {
        m_newEVMContractAddress = std::move(newEVMContractAddress);
    }

    std::optional<u256> createSalt() const override { return m_createSalt; }
    void setCreateSalt(u256 createSalt) override { m_createSalt = std::move(createSalt); }

    bool staticCall() const override { return m_staticCall; }
    void setStaticCall(bool staticCall) override { m_staticCall = staticCall; }

    Type m_type;
    int64_t m_contextID;
    int64_t m_gasAvailable;
    int32_t m_status;
    std::string m_message;
    bytes m_output;
    std::shared_ptr<std::vector<bcos::protocol::LogEntry>> m_logEntries;
    std::string m_to;
    std::string m_newEVMContractAddress;
    std::optional<u256> m_createSalt;
    bool m_staticCall;
};

class MockExecutionResultFactory : public bcos::protocol::ExecutionResultFactory
{
public:
    bcos::protocol::ExecutionResult::Ptr createExecutionResult() override
    {
        return std::make_shared<MockExecutionResult>();
    }
};

}  // namespace bcos::test