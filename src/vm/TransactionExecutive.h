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

    CallResults::Ptr execute(CallParameters::ConstPtr callParameters);

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

    int64_t m_baseGasRequired = 0;  ///< The base amount of gas requried for executing
                                    ///< this transaction.

    protocol::LogEntriesPtr m_logs =
        std::make_shared<protocol::LogEntries>();  ///< The log entries created by
                                                   ///< this transaction. Set by
                                                   ///< finalize().

    std::shared_ptr<wasm::GasInjector> m_gasInjector;
};

}  // namespace executor
}  // namespace bcos
