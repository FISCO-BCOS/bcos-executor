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
    using Coroutine = boost::coroutines2::coroutine<CallParameters::Ptr>;

    TransactionExecutive(std::shared_ptr<BlockContext> blockContext, std::string contractAddress,
        int64_t contextID,
        std::function<void(std::shared_ptr<TransactionExecutive> executive, CallParameters::Ptr&&)>
            callback)
      : m_blockContext(std::move(blockContext)),
        m_contractAddress(std::move(contractAddress)),
        m_contextID(contextID),
        m_callback(std::move(callback)),
        m_gasInjector(std::make_shared<wasm::GasInjector>(wasm::GetInstructionTable()))
    {}

    TransactionExecutive(TransactionExecutive const&) = delete;
    TransactionExecutive& operator=(TransactionExecutive) = delete;
    TransactionExecutive(TransactionExecutive&&) = default;
    TransactionExecutive& operator=(TransactionExecutive&&) = default;

    virtual ~TransactionExecutive() {}

    void start(CallParameters::Ptr callParameters);

    void pushMessage(CallParameters::Ptr callParameters)  // call by executor
    {
        (*m_pushMessage)(std::move(callParameters));
    }

    CallParameters::Ptr externalRequest(CallParameters::Ptr input);  // call by host context

    void reset()
    {
        if (m_logs)
        {
            m_logs->clear();
        }
    }

    std::shared_ptr<BlockContext> blockContext() { return m_blockContext; }

    int64_t contextID() { return m_contextID; }

    std::string_view contractAddress() { return m_contractAddress; }

private:
    CallParameters::Ptr execute(CallParameters::ConstPtr callParameters);
    std::tuple<std::shared_ptr<HostContext>, CallParameters::Ptr> call(
        CallParameters::ConstPtr callParameters);
    std::tuple<std::shared_ptr<HostContext>, CallParameters::Ptr> create(
        CallParameters::ConstPtr callParameters);
    CallParameters::Ptr go(std::shared_ptr<HostContext> hostContext);

    CallParameters::Ptr parseEVMCResult(bool isCreate, std::shared_ptr<Result> _result);

    void writeErrInfoToOutput(std::string const& errInfo);
    void updateGas(std::shared_ptr<precompiled::PrecompiledExecResult> _callResult);

    std::string getContractTableName(const std::string_view& _address, bool _isWasm);

    CallParameters::Ptr getPullMessage()  // call by host context
    {
        return m_pullMessage->get();
    }

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

    std::function<void(std::shared_ptr<TransactionExecutive> executive, CallParameters::Ptr&&)>
        m_callback;

    std::shared_ptr<wasm::GasInjector> m_gasInjector;

    std::unique_ptr<Coroutine::push_type> m_pushMessage;
    std::unique_ptr<Coroutine::pull_type> m_pullMessage;
};

}  // namespace executor
}  // namespace bcos
