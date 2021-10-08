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
 * @brief TransactionExecutor
 * @file TransactionExecutor.h
 * @author: xingqiangbai
 * @date: 2021-05-27
 */
#pragma once

#include "bcos-framework/interfaces/executor/ParallelTransactionExecutorInterface.h"
#include "bcos-framework/interfaces/protocol/Block.h"
#include "bcos-framework/interfaces/protocol/BlockFactory.h"
#include "bcos-framework/interfaces/protocol/Transaction.h"
#include "bcos-framework/interfaces/protocol/TransactionReceipt.h"
#include "bcos-framework/interfaces/storage/StorageInterface.h"
#include "bcos-framework/interfaces/txpool/TxPoolInterface.h"
#include "bcos-framework/libstorage/StateStorage.h"
#include "interfaces/crypto/Hash.h"
#include "interfaces/protocol/ProtocolTypeDef.h"
#include "tbb/concurrent_unordered_map.h"
#include <boost/function.hpp>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stack>
#include <thread>

namespace bcos
{
class ThreadPool;
namespace protocol
{
class TransactionReceipt;
}  // namespace protocol

namespace executor
{
enum ExecutorVersion : int32_t
{
    Version_3_0_0 = 1,
};

class TransactionExecutive;
class BlockContext;
class PrecompiledContract;
template <typename T, typename V>
class ClockCache;
struct FunctionAbi;
struct CallParameters;

using executionCallback = std::function<void(
    const Error::ConstPtr&, std::vector<protocol::ExecutionMessage::UniquePtr>&)>;

using ConflictFields = std::vector<bytes>;

class TransactionExecutor : public ParallelTransactionExecutorInterface,
                            public std::enable_shared_from_this<TransactionExecutor>
{
public:
    using Ptr = std::shared_ptr<TransactionExecutor>;
    using ConstPtr = std::shared_ptr<const TransactionExecutor>;
    using CallBackFunction = std::function<crypto::HashType(protocol::BlockNumber x)>;

    TransactionExecutor(txpool::TxPoolInterface::Ptr txpool,
        storage::TransactionalStorageInterface::Ptr backendStorage,
        protocol::ExecutionMessageFactory::Ptr executionMessageFactory,
        bcos::crypto::Hash::Ptr hashImpl, bool isWasm);

    virtual ~TransactionExecutor() {}

    void nextBlockHeader(const bcos::protocol::BlockHeader::ConstPtr& blockHeader,
        std::function<void(bcos::Error::UniquePtr&&)> callback) noexcept override;

    void executeTransaction(bcos::protocol::ExecutionMessage::UniquePtr input,
        std::function<void(bcos::Error::UniquePtr&&, bcos::protocol::ExecutionMessage::UniquePtr&&)>
            callback) noexcept override;

    void dagExecuteTransactions(
        const gsl::span<bcos::protocol::ExecutionMessage::UniquePtr>& inputs,
        std::function<void(
            bcos::Error::UniquePtr&&, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>&&)>
            callback) noexcept override;

    void call(bcos::protocol::ExecutionMessage::UniquePtr input,
        std::function<void(bcos::Error::UniquePtr&&, bcos::protocol::ExecutionMessage::UniquePtr&&)>
            callback) noexcept override;

    void getTableHashes(bcos::protocol::BlockNumber number,
        std::function<void(
            bcos::Error::UniquePtr&&, std::vector<std::tuple<std::string, crypto::HashType>>&&)>
            callback) noexcept override;

    /* ----- XA Transaction interface Start ----- */

    // Write data to storage uncommitted
    void prepare(const TwoPCParams& params,
        std::function<void(bcos::Error::Ptr&&)> callback) noexcept override;

    // Commit uncommitted data
    void commit(const TwoPCParams& params,
        std::function<void(bcos::Error::Ptr&&)> callback) noexcept override;

    // Rollback the changes
    void rollback(const TwoPCParams& params,
        std::function<void(bcos::Error::Ptr&&)> callback) noexcept override;

    /* ----- XA Transaction interface End ----- */

    // drop all status
    void reset(std::function<void(bcos::Error::Ptr&&)> callback) noexcept override;

private:
    protocol::Transaction::Ptr createTransaction(
        const bcos::protocol::ExecutionMessage::UniquePtr& input);
    std::shared_ptr<BlockContext> createBlockContext(
        const protocol::BlockHeader::ConstPtr& currentHeader,
        storage::StateStorage::Ptr tableFactory);

    void asyncExecute(bcos::protocol::ExecutionMessage::UniquePtr input, bool staticCall,
        std::function<void(bcos::Error::UniquePtr&&, bcos::protocol::ExecutionMessage::UniquePtr&&)>
            callback);

    void onCallResultsCallback(std::shared_ptr<TransactionExecutive> executive,
        std::unique_ptr<CallParameters> callResults);

    std::string newEVMAddress(
        const std::string_view& sender, int64_t blockNumber, int64_t contextID);
    std::string newEVMAddress(
        const std::string_view& _sender, bytesConstRef _init, u256 const& _salt);

    std::unique_ptr<CallParameters> createCallParameters(
        const bcos::protocol::ExecutionMessage& inputs, const BlockContext& blockContext,
        bool staticCall);

    std::unique_ptr<CallParameters> createCallParameters(
        std::shared_ptr<bcos::protocol::Transaction>&& tx, const BlockContext& blockContext,
        int64_t contextID);

    std::unique_ptr<CallParameters> createCallParameters(
        const bcos::protocol::ExecutionMessage& input, bcos::protocol::Transaction::Ptr&& tx,
        const BlockContext& blockContext);

    std::optional<std::vector<bcos::bytes>> decodeConflictFields(
        const FunctionAbi& functionAbi, bcos::protocol::Transaction* transaction);

    txpool::TxPoolInterface::Ptr m_txpool;
    std::shared_ptr<storage::TransactionalStorageInterface> m_backendStorage;
    protocol::ExecutionMessageFactory::Ptr m_executionMessageFactory;
    std::shared_ptr<BlockContext> m_blockContext = nullptr;
    crypto::Hash::Ptr m_hashImpl;
    bool m_isWasm = false;
    const ExecutorVersion m_version;
    std::shared_ptr<ClockCache<bcos::bytes, FunctionAbi>> m_abiCache = nullptr;

    struct State
    {
        bcos::protocol::BlockNumber number;
        bcos::storage::StateStorage::Ptr storage;
    };

    std::list<State> m_stateStorages;  // TODO: need lock to deal with
                                       // nextBlock and prepare?

    std::list<State>::const_iterator m_lastUncommitedIterator;  // last uncommited storage

    std::shared_ptr<std::map<std::string, std::shared_ptr<PrecompiledContract>>>
        m_precompiledContract;
};

enum ConflictFieldKind : std::uint8_t
{
    All = 0,
    Len,
    Env,
    Var,
    Const,
};

enum EnvKind : std::uint8_t
{
    Caller = 0,
    Origin,
    Now,
    BlockNumber,
    Addr,
};
}  // namespace executor
}  // namespace bcos
