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
 * @file Executive.h
 * @author: xingqiangbai
 * @date: 2021-05-24
 */

#pragma once

#include "../libprecompiled/PrecompiledResult.h"
#include "Common.h"
#include "BlockContext.h"
#include "bcos-framework/interfaces/protocol/BlockHeader.h"
#include "bcos-framework/interfaces/protocol/Transaction.h"
#include "bcos-framework/libprotocol/TransactionStatus.h"
#include "gas_meter/GasInjector.h"
#include <functional>

namespace bcos
{
namespace storage
{
class TableFactoryInterface;
}

namespace executor
{
class Block;
class Result;
}  // namespace executor
namespace precompiled
{
struct PrecompiledExecResult;
}

/**
 * @brief Message-call/contract-creation executor; useful for executing transactions.
 *
 * Two ways of using this class - either as a transaction executive or a CALL/CREATE executive.
 *
 * In the first use, after construction, begin with initialize(), then execute() and end with
 * finalize(). Call go() after execute() only if it returns false.
 *
 * In the second use, after construction, begin with call() or create() and end with
 * accrueSubState(). Call go() after call()/create() only if it returns false.
 *
 * Example:
 * @code
 * Executive e(state, blockchain, 0);
 * e.initialize(transaction);
 * if (!e.execute())
 *    e.go();
 * e.finalize();
 * @endcode
 */
namespace executor
{
class HostContext;
class StateInterface;
class Executive
{
public:
    using Ptr = std::shared_ptr<Executive>;
    /// Simple constructor; executive will operate on given state, with the given environment info.
    Executive(const std::shared_ptr<BlockContext>& _envInfo, unsigned _level = 0)
      : m_s(_envInfo->getState()),
        m_envInfo(_envInfo),
        m_hashImpl(_envInfo->hashHandler()),
        m_depth(_level),
        m_gasInjector(std::make_shared<wasm::GasInjector>(wasm::GetInstructionTable()))
    {}

    Executive(Executive const&) = delete;
    void operator=(Executive) = delete;

    void initialize(protocol::Transaction::ConstPtr _transaction);
    /// Finalise a transaction previously set up with initialize().
    /// @warning Only valid after initialize() and execute(), and possibly go().
    /// @returns true if the outermost execution halted normally, false if exceptionally halted.
    bool finalize();
    /// Begins execution of a transaction. You must call finalize() following this.
    /// @returns true if the transaction is done, false if go() must be called.

    bool execute();
    /// @returns the transaction from initialize().
    /// @warning Only valid after initialize().
    protocol::Transaction::ConstPtr tx() const { return m_t; }
    /// @returns the log entries created by this operation.
    /// @warning Only valid after finalise().
    protocol::LogEntriesPtr const& logs() const { return m_logs; }
    /// @returns total gas used in the transaction/operation.
    /// @warning Only valid after finalise().
    u256 gasUsed() const;

    owning_bytes_ref takeOutput() { return std::move(m_output); }

    /// Set up the executive for evaluating a bare CREATE (contract-creation) operation.
    /// @returns false iff go() must be called (and thus a VM execution in required).
    bool create(const std::string_view& _txSender, u256 const& _gas, bytesConstRef _code,
        const std::string_view& _originAddress);
    /// @returns false iff go() must be called (and thus a VM execution in required).
    bool createOpcode(const std::string_view& _sender, u256 const& _gas, bytesConstRef _code,
        const std::string_view& _originAddress);
    /// @returns false iff go() must be called (and thus a VM execution in required).
    bool create2Opcode(const std::string_view& _sender, u256 const& _gas, bytesConstRef _code,
        const std::string_view& _originAddress, u256 const& _salt);
    /// Set up the executive for evaluating a bare CALL (message call) operation.
    /// @returns false iff go() must be called (and thus a VM execution in required).
    bool call(const std::string& _receiveAddress, const std::string& _txSender,
        bytesConstRef _txData, u256 const& _gas);
    bool call(executor::CallParameters const& _cp, const std::string& _origin);
    /// Finalise an operation through accruing the substate into the parent context.
    void accrueSubState(SubState& _parentContext);

    /// Executes (or continues execution of) the VM.
    /// @returns false iff go() must be called again to finish the transaction.
    bool go();

    /// @returns gas remaining after the transaction/operation. Valid after the transaction has been
    /// executed.
    u256 gas() const { return m_remainGas; }
    protocol::TransactionStatus status() const { return m_excepted; }
    /// @returns the new address for the created contract in the CREATE operation.
    std::string newAddress() const { return m_newAddress; }

    /// Revert all changes made to the state by this execution.
    void revert();

    /// print exception to log
    void loggingException();

    void reset()
    {
        m_context = nullptr;
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
        m_t.reset();
    }

    std::shared_ptr<BlockContext> getEnvInfo() { return m_envInfo; }
    /// @returns false iff go() must be called (and thus a VM execution in required).
    bool executeCreate(const std::string_view& _txSender, const std::string_view& _originAddress,
        const std::string& _newAddress, u256 const& _gas, bytesConstRef _code,
        bytesConstRef constructorParams = bytesConstRef());

private:
    void parseEVMCResult(std::shared_ptr<Result> _result);

    void writeErrInfoToOutput(std::string const& errInfo);

    void updateGas(std::shared_ptr<precompiled::PrecompiledExecResult> _callResult);

    std::shared_ptr<StateInterface> m_s;  ///< The state to which this operation/transaction is
                                          ///< applied.
    std::shared_ptr<BlockContext> m_envInfo;  ///< Information on the runtime environment.
    crypto::Hash::Ptr m_hashImpl;
    std::shared_ptr<HostContext> m_context;  ///< The VM externality object for the VM execution
                                             ///< or null if no VM is required. shared_ptr used
                                             ///< only to allow HostContext forward reference.
                                             ///< This field does *NOT* survive this object.
    owning_bytes_ref m_output;               ///< Execution output.

    unsigned m_depth = 0;  ///< The context's call-depth.
    protocol::TransactionStatus m_excepted =
        protocol::TransactionStatus::None;  ///< Details if the VM's execution resulted in an
                                            ///< exception.
    std::stringstream m_exceptionReason;

    int64_t m_baseGasRequired;  ///< The base amount of gas requried for executing this transaction.
    u256 m_remainGas = 0;       ///< The gas for EVM code execution. Initial amount before go()
                                ///< execution, final amount after go() execution.

    protocol::Transaction::ConstPtr m_t;  ///< The original transaction. Set by setup().
    protocol::LogEntriesPtr m_logs =
        std::make_shared<protocol::LogEntries>();  ///< The log entries created by this transaction.
                                                   ///< Set by finalize().

    bool m_isCreation = false;
    std::string m_newAddress;
    size_t m_savepoint = 0;

    std::shared_ptr<wasm::GasInjector> m_gasInjector;
};

}  // namespace executor
}  // namespace bcos
