/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief executive of vm
 * @file TransactionExecutive.h
 * @author: xingqiangbai
 * @date: 2021-05-24
 */

#pragma once

#include "../precompiled/PrecompiledResult.h"
#include "BlockContext.h"
#include "Common.h"
#include "bcos-framework/interfaces/executor/ExecutionResult.h"
#include "bcos-framework/interfaces/protocol/BlockHeader.h"
#include "bcos-framework/interfaces/protocol/Transaction.h"
#include "bcos-framework/libprotocol/TransactionStatus.h"
#include "gas_meter/GasInjector.h"
#include <functional>

namespace bcos
{
namespace storage
{
class StateStorage;
}

namespace executor
{
class Block;
class Result;

using returnCallback =
    std::function<void(bcos::Error::Ptr&&, bcos::protocol::ExecutionResult::Ptr&&)>;
}  // namespace executor
namespace precompiled
{
struct PrecompiledExecResult;
}

/**
 * @brief Message-call/contract-creation executor; useful for executing
 * transactions.
 *
 * Two ways of using this class - either as a transaction executive or a
 * CALL/CREATE executive.
 *
 * In the first use, after construction, begin with initialize(), then execute()
 * and end with finalize(). Call go() after execute() only if it returns false.
 *
 * In the second use, after construction, begin with call() or create(). Call
 * go() after call()/create() only if it returns false.
 *
 * Example:
 * @code
 * TransactionExecutive e(state, blockchain, 0);
 * e.initialize(transaction);
 * if (!e.execute())
 *    e.go();
 * e.finalize();
 * @endcode
 */
namespace executor
{
class HostContext;

class TransactionExecutive : public std::enable_shared_from_this<TransactionExecutive>
{
public:
    using Ptr = std::shared_ptr<TransactionExecutive>;
    /// Simple constructor; executive will operate on given state, with the given
    /// environment info.
    TransactionExecutive(
        std::shared_ptr<BlockContext> _blockContext, int64_t _contextID, unsigned _level = 0)
      : m_blockContext(std::move(_blockContext)),
        m_contextID(_contextID),
        m_hashImpl(_blockContext->hashHandler()),
        m_depth(_level),
        m_gasInjector(std::make_shared<wasm::GasInjector>(wasm::GetInstructionTable()))
    {}

    TransactionExecutive(TransactionExecutive const&) = delete;
    virtual ~TransactionExecutive() {}
    void operator=(TransactionExecutive) = delete;

    void initialize(protocol::Transaction::ConstPtr _transaction);
    /// Finalise a transaction previously set up with initialize().
    /// @warning Only valid after initialize() and execute(), and possibly go().
    /// @returns true if the outermost execution halted normally, false if
    /// exceptionally halted.

    void setReturnCallback(returnCallback _callback) { m_returnCallback = _callback; }

    void callReturnCallback(Error::Ptr e, protocol::ExecutionResult::Ptr result)
    {
        m_returnCallback(std::move(e), std::move(result));
    }

    bool continueExecution(
        bytes&& output, int32_t status, int64_t gasLeft, std::string_view newAddress);

    evmc_result waitReturnValue(Error::Ptr e, protocol::ExecutionResult::Ptr result);
    bool isFinished() { return m_finished; }

    bool finalize();
    /// Begins execution of a transaction. You must call finalize() following
    /// this.
    /// @returns true if the transaction is done, false if go() must be called.

    bool execute(bool _staticCall);
    /// @returns the transaction from initialize().
    /// @warning Only valid after initialize().
    protocol::Transaction::ConstPtr tx() const { return m_transaction; }
    /// @returns the log entries created by this operation.
    /// @warning Only valid after finalise().
    protocol::LogEntriesPtr const& logs() const { return m_logs; }
    /// @returns total gas used in the transaction/operation.
    /// @warning Only valid after finalise().
    u256 gasLeft() const;

    owning_bytes_ref takeOutput() { return std::move(m_output); }

    std::string newEVMAddress(const std::string_view& _sender);
    std::string newEVMAddress(
        const std::string_view& _sender, bytesConstRef _init, u256 const& _salt);
    /// Set up the executive for evaluating a bare CREATE (contract-creation)
    /// operation.
    /// @returns false iff go() must be called (and thus a VM execution in
    /// required).
    bool create(const std::string_view& _txSender, int64_t _gas, bytesConstRef _code,
        const std::string_view& _originAddress);
    /// @returns false iff go() must be called (and thus a VM execution in
    /// required).
    bool createOpcode(const std::string_view& _sender, int64_t _gas, bytesConstRef _code,
        const std::string_view& _originAddress);
    /// @returns false iff go() must be called (and thus a VM execution in
    /// required).
    bool create2Opcode(const std::string_view& _sender, int64_t _gas, bytesConstRef _code,
        const std::string_view& _originAddress, u256 const& _salt);
    /// Set up the executive for evaluating a bare CALL (message call) operation.
    /// @returns false iff go() must be called (and thus a VM execution in
    /// required).
    bool call(const std::string& _receiveAddress, const std::string& _txSender,
        bytesConstRef _txData, int64_t _gas, bool _staticCall);
    bool call(CallParameters _cp);

    /// Executes (or continues execution of) the VM.
    /// @returns false iff go() must be called again to finish the transaction.
    bool go();

    /// @returns gas remaining after the transaction/operation. Valid after the
    /// transaction has been executed.
    u256 gas() const { return m_remainGas; }
    protocol::TransactionStatus status() const { return m_excepted; }
    /// @returns the new address for the created contract in the CREATE operation.
    std::string newAddress() const;

    /// Revert all changes made to the state by this execution.
    void revert();

    /// print exception to log
    void loggingException();

    void reset()
    {
        m_output = owning_bytes_ref();
        m_depth = 0;
        m_excepted = protocol::TransactionStatus::None;
        m_exceptionReason.clear();
        m_baseGasRequired = 0;
        m_remainGas = 0;
        m_isCreation = false;
        m_newAddress = std::string();
        m_savepoint = 0;
        if (m_logs)
        {
            m_logs->clear();
        }
    }

    std::shared_ptr<BlockContext> blockContext() { return m_blockContext; }
    /// @returns false iff go() must be called (and thus a VM execution in
    /// required).
    bool executeCreate(const std::string_view& _txSender, const std::string_view& _originAddress,
        const std::string& _newAddress, int64_t _gas, bytesConstRef _code,
        bytesConstRef constructorParams = bytesConstRef());

    int64_t getContextID() { return m_contextID; }

    void setCallCreate(bool _callCreate) { m_callCreate = _callCreate; }

private:
    void parseEVMCResult(std::shared_ptr<Result> _result);
    void writeErrInfoToOutput(std::string const& errInfo);
    void updateGas(std::shared_ptr<precompiled::PrecompiledExecResult> _callResult);

    std::string getContractTableName(
        const std::string_view& _address, bool _isWasm, crypto::Hash::Ptr _hashImpl);

    std::shared_ptr<BlockContext> m_blockContext;  ///< Information on the runtime environment.
    int64_t m_contextID = 0;
    crypto::Hash::Ptr m_hashImpl;
    std::shared_ptr<HostContext> m_hostContext;

    owning_bytes_ref m_output;  ///< Execution output.

    unsigned m_depth = 0;  ///< The context's call-depth.
    protocol::TransactionStatus m_excepted =
        protocol::TransactionStatus::None;  ///< Details if the VM's execution
                                            ///< resulted in an exception.
    std::string m_exceptionReason;

    int64_t m_baseGasRequired;  ///< The base amount of gas requried for executing
                                ///< this transaction.
    int64_t m_remainGas = 0;    ///< The gas for EVM code execution. Initial amount before go()
                                ///< execution, final amount after go() execution.

    protocol::Transaction::ConstPtr m_transaction;  ///< The original transaction. Set by setup().
    protocol::LogEntriesPtr m_logs =
        std::make_shared<protocol::LogEntries>();  ///< The log entries created by
                                                   ///< this transaction. Set by
                                                   ///< finalize().

    std::string m_newAddress;
    size_t m_savepoint = 0;
    std::shared_ptr<wasm::GasInjector> m_gasInjector;
    executor::returnCallback m_returnCallback = nullptr;
    std::function<void(
        bytes&& output, int32_t status, int64_t gasLeft, std::string_view newAddress)>
        m_waitResult = nullptr;

    bool m_isCreation = false;
    bool m_finished = false;
    bool m_callCreate = false;  // if contract create contract true, else false
};

}  // namespace executor
}  // namespace bcos
