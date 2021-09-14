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

    TransactionExecutive(std::shared_ptr<BlockContext> blockContext, CallParameters callParameters,
        int64_t contextID, unsigned level = 0)
      : m_blockContext(std::move(blockContext)),
        m_callParameters(std::move(callParameters)),
        m_contextID(contextID),
        m_depth(level),
        m_gasInjector(std::make_shared<wasm::GasInjector>(wasm::GetInstructionTable()))
    {}

    TransactionExecutive(TransactionExecutive const&) = delete;
    virtual ~TransactionExecutive() {}
    void operator=(TransactionExecutive) = delete;

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

    bool execute();
    /// @returns the log entries created by this operation.
    /// @warning Only valid after finalise().
    protocol::LogEntriesPtr const& logs() const { return m_logs; }
    /// @returns total gas used in the transaction/operation.
    /// @warning Only valid after finalise().
    u256 gasLeft() const;

    owning_bytes_ref takeOutput() { return std::move(m_output); }

    /// Set up the executive for evaluating a bare CREATE (contract-creation)
    /// operation.
    /// @returns false iff go() must be called (and thus a VM execution in
    /// required).
    bool create();
    /// Set up the executive for evaluating a bare CALL (message call) operation.
    /// @returns false iff go() must be called (and thus a VM execution in
    /// required).
    bool call();

    /// Executes (or continues execution of) the VM.
    /// @returns false iff go() must be called again to finish the transaction.
    bool go();

    /// @returns gas remaining after the transaction/operation. Valid after the
    /// transaction has been executed.
    int64_t gas() const { return m_remainGas; }
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
    bool executeCreate();

    int64_t getContextID() { return m_contextID; }

    bool callCreate() { return m_callCreate; }
    void setCallCreate(bool _callCreate) { m_callCreate = _callCreate; }

    std::string_view contractAddress() { return m_callParameters.codeAddress; }

    const CallParameters& callParameters() { return m_callParameters; }

private:
    void parseEVMCResult(std::shared_ptr<Result> _result);
    void writeErrInfoToOutput(std::string const& errInfo);
    void updateGas(std::shared_ptr<precompiled::PrecompiledExecResult> _callResult);

    std::string getContractTableName(
        const std::string_view& _address, bool _isWasm, crypto::Hash::Ptr _hashImpl);

    std::shared_ptr<BlockContext> m_blockContext;  ///< Information on the runtime environment.
    CallParameters m_callParameters;
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

    // protocol::Transaction::ConstPtr m_transaction;  ///< The original transaction. Set by
    // setup().
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
