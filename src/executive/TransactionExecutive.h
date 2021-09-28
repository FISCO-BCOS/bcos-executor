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

#include "../Common.h"
#include "../precompiled/PrecompiledResult.h"
#include "../vm/gas_meter/GasInjector.h"
#include "BlockContext.h"
#include "CoroutineStorageWrapper.h"
#include "bcos-executor/TransactionExecutor.h"
#include "bcos-framework/interfaces/executor/ExecutionMessage.h"
#include "bcos-framework/interfaces/protocol/BlockHeader.h"
#include "bcos-framework/interfaces/protocol/Transaction.h"
#include "bcos-framework/libprotocol/TransactionStatus.h"
#include <boost/coroutine2/all.hpp>
#include <boost/coroutine2/coroutine.hpp>
#include <functional>
#include <variant>

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

    using CallMessage = CallParameters::UniquePtr;

    using CoroutineMessage = std::variant<CallMessage, GetPrimaryKeysReponse, GetRowResponse,
        GetRowsResponse, SetRowResponse, OpenTableResponse>;

    using Coroutine = boost::coroutines2::coroutine<CoroutineMessage>;

    TransactionExecutive(std::weak_ptr<BlockContext> blockContext, std::string contractAddress,
        int64_t contextID, int64_t seq,
        std::function<void(
            std::shared_ptr<TransactionExecutive> executive, CallParameters::UniquePtr&&)>
            externalCallCallback)
      : m_blockContext(std::move(blockContext)),
        m_contractAddress(std::move(contractAddress)),
        m_contextID(contextID),
        m_seq(seq),
        m_externalCallCallback(std::move(externalCallCallback)),
        m_gasInjector(std::make_shared<wasm::GasInjector>(wasm::GetInstructionTable()))
    {
        m_recoder = m_blockContext.lock()->storage()->newRecoder();
    }

    TransactionExecutive(TransactionExecutive const&) = delete;
    TransactionExecutive& operator=(TransactionExecutive) = delete;
    TransactionExecutive(TransactionExecutive&&) = delete;
    TransactionExecutive& operator=(TransactionExecutive&&) = delete;

    virtual ~TransactionExecutive() {}

    void start();  // start a new corountine to execute
                   // parameters

    void pushMessage(CoroutineMessage message)  // call by executor
    {
        (*m_pushMessage)(std::move(message));
    }

    // void switchAndExecute(std::function<void()> callback);

    // External call request
    CallParameters::UniquePtr externalCall(CallParameters::UniquePtr input);  // call by
                                                                              // hostContext

    CoroutineStorageWrapper<CoroutineMessage>& storage() { return *m_storageWrapper; }

    std::weak_ptr<BlockContext> blockContext() { return m_blockContext; }

    int64_t contextID() { return m_contextID; }
    int64_t seq() { return m_seq; }

    std::string_view contractAddress() { return m_contractAddress; }

    CallParameters::UniquePtr execute(
        CallParameters::UniquePtr callParameters);  // execute parameters in
                                                    // current corounitine

private:
    std::tuple<std::unique_ptr<HostContext>, CallParameters::UniquePtr> call(
        CallParameters::UniquePtr callParameters);
    std::tuple<std::unique_ptr<HostContext>, CallParameters::UniquePtr> create(
        CallParameters::UniquePtr callParameters);
    CallParameters::UniquePtr go(HostContext& hostContext);

    void revert();

    CallParameters::UniquePtr parseEVMCResult(bool isCreate, const Result& _result);

    void writeErrInfoToOutput(std::string const& errInfo);
    void updateGas(std::shared_ptr<precompiled::PrecompiledExecResult> _callResult);

    std::string getContractTableName(const std::string_view& _address, bool _isWasm);

    std::weak_ptr<BlockContext> m_blockContext;  ///< Information on the runtime environment.
    std::string m_contractAddress;
    int64_t m_contextID;
    int64_t m_seq;
    crypto::Hash::Ptr m_hashImpl;

    int64_t m_baseGasRequired = 0;  ///< The base amount of gas requried for executing
                                    ///< this transaction.

    std::function<void(
        std::shared_ptr<TransactionExecutive> executive, CallParameters::UniquePtr&&)>
        m_externalCallCallback;

    std::shared_ptr<wasm::GasInjector> m_gasInjector;

    std::unique_ptr<Coroutine::push_type> m_pushMessage;
    std::unique_ptr<Coroutine::pull_type> m_pullMessage;
    bcos::storage::StateStorage::Recoder::Ptr m_recoder;
    std::set<std::string> m_keyLocks;
    std::unique_ptr<CoroutineStorageWrapper<CoroutineMessage>> m_storageWrapper;
};

}  // namespace executor
}  // namespace bcos
