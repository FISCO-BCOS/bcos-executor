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
#include "bcos-framework/interfaces/executor/ExecutionParams.h"
#include "bcos-framework/interfaces/executor/ExecutionResult.h"
#include "bcos-framework/interfaces/protocol/BlockHeader.h"
#include "bcos-framework/interfaces/protocol/Transaction.h"
#include "bcos-framework/libprotocol/TransactionStatus.h"
#include "gas_meter/GasInjector.h"
#include <boost/coroutine2/all.hpp>
#include <boost/coroutine2/coroutine.hpp>
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

namespace executor
{
class HostContext;

class TransactionExecutive : public std::enable_shared_from_this<TransactionExecutive>
{
public:
    using Ptr = std::shared_ptr<TransactionExecutive>;
    using Coroutine =
        boost::coroutines2::coroutine<std::tuple<bcos::protocol::ExecutionParams::ConstPtr,
            std::function<void(bcos::Error::Ptr&&, bcos::protocol::ExecutionResult::Ptr&&)>>>;

    TransactionExecutive(
        std::shared_ptr<BlockContext> blockContext, std::string contractAddress, int64_t contextID)
      : m_blockContext(std::move(blockContext)),
        m_contractAddress(std::move(contractAddress)),
        m_contextID(contextID),
        m_gasInjector(std::make_shared<wasm::GasInjector>(wasm::GetInstructionTable()))
    {}

    TransactionExecutive(TransactionExecutive const&) = delete;
    virtual ~TransactionExecutive() {}
    void operator=(TransactionExecutive) = delete;

    // void setReturnCallback(returnCallback _callback) { m_returnCallback = _callback; }

    // void callReturnCallback(Error::Ptr e, protocol::ExecutionResult::Ptr result)
    // {
    //     m_returnCallback(std::move(e), std::move(result));
    // }

    // bool continueExecution(
    //     bytes&& output, int32_t status, int64_t gasLeft, std::string_view newAddress);

    // evmc_result waitReturnValue(Error::Ptr e, protocol::ExecutionResult::Ptr result);

    CallResults::Ptr execute(CallParameters::ConstPtr callParameters);

    /// Revert all changes made to the state by this execution.
    // void revert();

    void reset()
    {
        // m_output = owning_bytes_ref();
        // m_excepted = protocol::TransactionStatus::None;
        // m_exceptionReason.clear();
        // m_baseGasRequired = 0;
        // m_remainGas = 0;
        // m_newAddress = std::string();
        // m_savepoint = 0;
        if (m_logs)
        {
            m_logs->clear();
        }
    }

    std::shared_ptr<BlockContext> blockContext() { return m_blockContext; }

    int64_t contextID() { return m_contextID; }

    std::string_view contractAddress() { return m_contractAddress; }

private:
    std::tuple<std::shared_ptr<HostContext>, CallResults::Ptr> call(
        CallParameters::ConstPtr callParameters);
    std::tuple<std::shared_ptr<HostContext>, CallResults::Ptr> create(
        CallParameters::ConstPtr callParameters);
    CallResults::Ptr go(std::shared_ptr<HostContext> hostContext);

    CallResults::Ptr parseEVMCResult(bool isCreate, std::shared_ptr<Result> _result);

    void writeErrInfoToOutput(std::string const& errInfo);
    void updateGas(std::shared_ptr<precompiled::PrecompiledExecResult> _callResult);

    std::string getContractTableName(
        const std::string_view& _address, bool _isWasm, crypto::Hash::Ptr _hashImpl);

    std::shared_ptr<BlockContext> m_blockContext;  ///< Information on the runtime environment.
    std::string m_contractAddress;
    int64_t m_contextID = 0;
    crypto::Hash::Ptr m_hashImpl;

    // owning_bytes_ref m_output;  ///< Execution output.

    // protocol::TransactionStatus m_excepted =
    //     protocol::TransactionStatus::None;  ///< Details if the VM's execution
    //                                         ///< resulted in an exception.
    // std::string m_exceptionReason;

    int64_t m_baseGasRequired = 0;  ///< The base amount of gas requried for executing
                                    ///< this transaction.

    protocol::LogEntriesPtr m_logs =
        std::make_shared<protocol::LogEntries>();  ///< The log entries created by
                                                   ///< this transaction. Set by
                                                   ///< finalize().

    // std::string m_newAddress;
    // size_t m_savepoint = 0;
    std::shared_ptr<wasm::GasInjector> m_gasInjector;

    // executor::returnCallback m_returnCallback = nullptr;
    // std::function<void(
    //     bytes&& output, int32_t status, int64_t gasLeft, std::string_view newAddress)>
    //     m_waitResult = nullptr;

    // bool m_finished = false;
};

}  // namespace executor
}  // namespace bcos
