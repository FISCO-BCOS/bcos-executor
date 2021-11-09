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
#include "BlockContext.h"
#include "CoroutineStorageWrapper.h"
#include "bcos-executor/TransactionExecutor.h"
#include "bcos-framework/interfaces/executor/ExecutionMessage.h"
#include "bcos-framework/interfaces/protocol/BlockHeader.h"
#include "bcos-framework/interfaces/protocol/Transaction.h"
#include "bcos-framework/libprotocol/TransactionStatus.h"
#include <bcos-framework/libcodec/abi/ContractABICodec.h>
#include <boost/algorithm/string/case_conv.hpp>
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
        std::function<void(std::shared_ptr<BlockContext>, std::shared_ptr<TransactionExecutive>,
            std::unique_ptr<CallParameters>,
            std::function<void(Error::UniquePtr, std::unique_ptr<CallParameters>)>)>
            externalCallCallback, std::shared_ptr<wasm::GasInjector>& gasInjector)
      : m_blockContext(std::move(blockContext)),
        m_contractAddress(std::move(contractAddress)),
        m_contextID(contextID),
        m_seq(seq),
        m_externalCallFunction(std::move(externalCallCallback)),
        m_gasInjector(gasInjector)
        // m_gasInjector(std::make_shared<wasm::GasInjector>(wasm::GetInstructionTable()))
    {

        m_recoder = m_blockContext.lock()->storage()->newRecoder();
        m_hashImpl = m_blockContext.lock()->hashHandler();
    }

    TransactionExecutive(TransactionExecutive const&) = delete;
    TransactionExecutive& operator=(TransactionExecutive) = delete;
    TransactionExecutive(TransactionExecutive&&) = delete;
    TransactionExecutive& operator=(TransactionExecutive&&) = delete;

    virtual ~TransactionExecutive() = default;

    void start(CallParameters::UniquePtr input);  // start a new coroutine to execute

    void pushMessage(CoroutineMessage message)  // call by executor
    {
        (*m_pushMessage)(std::move(message));
    }

    // External call request
    CallParameters::UniquePtr externalCall(CallParameters::UniquePtr input);  // call by
                                                                              // hostContext

    // External request key locks
    void externalAcquireKeyLocks(std::string acquireKeyLock);

    CoroutineStorageWrapper<CoroutineMessage>& storage() {
        assert(m_storageWrapper);
        return *m_storageWrapper;
    }

    std::weak_ptr<BlockContext> blockContext() { return m_blockContext; }

    int64_t contextID() { return m_contextID; }
    int64_t seq() { return m_seq; }

    std::string_view contractAddress() { return m_contractAddress; }

    CallParameters::UniquePtr execute(
        CallParameters::UniquePtr callParameters);  // execute parameters in
                                                    // current corouitine

    bool isPrecompiled(const std::string& _address) const;

    std::shared_ptr<precompiled::Precompiled> getPrecompiled(const std::string& _address) const;

    void setConstantPrecompiled(
        const std::string& _address, std::shared_ptr<precompiled::Precompiled> precompiled);

    void setBuiltInPrecompiled(std::shared_ptr<const std::set<std::string>> _builtInPrecompiled)
    {
        m_builtInPrecompiled = std::move(_builtInPrecompiled);
    }

    bool isBuiltInPrecompiled(const std::string& _a) const;

    bool isEthereumPrecompiled(const std::string& _a) const;

    std::pair<bool, bytes> executeOriginPrecompiled(const std::string& _a, bytesConstRef _in) const;

    int64_t costOfPrecompiled(const std::string& _a, bytesConstRef _in) const;

    void setEVMPrecompiled(
        std::shared_ptr<const std::map<std::string, std::shared_ptr<PrecompiledContract>>>
            precompiledContract);

    void setConstantPrecompiled(
        const std::map<std::string, std::shared_ptr<precompiled::Precompiled>>
            _constantPrecompiled);

    std::shared_ptr<precompiled::PrecompiledExecResult> execPrecompiled(const std::string& address,
        bytesConstRef param, const std::string& origin, const std::string& sender);

private:
    std::tuple<std::unique_ptr<HostContext>, CallParameters::UniquePtr> call(
        CallParameters::UniquePtr callParameters);
    std::tuple<std::unique_ptr<HostContext>, CallParameters::UniquePtr> callPrecompiled(
        CallParameters::UniquePtr callParameters);
    std::tuple<std::unique_ptr<HostContext>, CallParameters::UniquePtr> create(
        CallParameters::UniquePtr callParameters);
    CallParameters::UniquePtr go(
        HostContext& hostContext, CallParameters::UniquePtr extraData = nullptr);

    void revert();

    CallParameters::UniquePtr parseEVMCResult(
        CallParameters::UniquePtr callResults, const Result& _result);

    void writeErrInfoToOutput(std::string const& errInfo, bytes& output)
    {
        bcos::codec::abi::ContractABICodec abi(m_hashImpl);
        auto codecOutput = abi.abiIn("Error(string)", errInfo);
        output = std::move(codecOutput);
    }

    inline std::string getContractTableName(const std::string_view& _address)
    {
        std::string addressLower(_address);
        boost::algorithm::to_lower(addressLower);

        std::string address = (_address[0] == '/') ? addressLower.substr(1) : addressLower;

        return std::string("/apps/").append(address);
    }

    void creatAuthTable(
        std::string_view _tableName, std::string_view _origin, std::string_view _sender);

    bool buildBfsPath(std::string const& _absoluteDir);

    std::weak_ptr<BlockContext> m_blockContext;  ///< Information on the runtime environment.
    std::map<std::string, std::shared_ptr<precompiled::Precompiled>> m_constantPrecompiled;
    std::shared_ptr<const std::map<std::string, std::shared_ptr<PrecompiledContract>>>
        m_evmPrecompiled;
    std::shared_ptr<const std::set<std::string>> m_builtInPrecompiled;

    std::string m_contractAddress;
    int64_t m_contextID;
    int64_t m_seq;
    crypto::Hash::Ptr m_hashImpl;

    ///< The base amount of gas required for executing this transaction.
    // TODO: not used
    //int64_t m_baseGasRequired = 0;


    std::function<void(std::shared_ptr<BlockContext> blockContext,
        std::shared_ptr<TransactionExecutive> executive,
        std::unique_ptr<CallParameters> callResults,
        std::function<void(Error::UniquePtr, std::unique_ptr<CallParameters>)> callback)>
        m_externalCallFunction;

    std::shared_ptr<wasm::GasInjector> m_gasInjector = nullptr;

    std::unique_ptr<Coroutine::push_type> m_pushMessage;
    std::unique_ptr<Coroutine::pull_type> m_pullMessage;
    bcos::storage::StateStorage::Recoder::Ptr m_recoder;
    std::unique_ptr<CoroutineStorageWrapper<CoroutineMessage>> m_storageWrapper;
    bool m_finished = false;
};

}  // namespace executor
}  // namespace bcos
