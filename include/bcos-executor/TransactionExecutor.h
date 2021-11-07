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
 * @brief TransactionExecutor
 * @file TransactionExecutor.h
 * @author: ancelmo
 * @date: 2021-10-16
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
#include <tbb/concurrent_hash_map.h>
#include <tbb/spin_mutex.h>
#include <boost/function.hpp>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stack>
#include <thread>

namespace bcos
{
namespace precompiled
{
class Precompiled;
struct PrecompiledExecResult;
}  // namespace precompiled

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

    TransactionExecutor(txpool::TxPoolInterface::Ptr txpool,
        storage::MergeableStorageInterface::Ptr cachedStorage,
        storage::TransactionalStorageInterface::Ptr backendStorage,
        protocol::ExecutionMessageFactory::Ptr executionMessageFactory,
        bcos::crypto::Hash::Ptr hashImpl, bool isWasm);

    virtual ~TransactionExecutor() {}

    void nextBlockHeader(const bcos::protocol::BlockHeader::ConstPtr& blockHeader,
        std::function<void(bcos::Error::UniquePtr)> callback) override;

    void executeTransaction(bcos::protocol::ExecutionMessage::UniquePtr input,
        std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
            callback) override;

    void dagExecuteTransactions(gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,
        std::function<void(
            bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
            callback) override;

    void call(bcos::protocol::ExecutionMessage::UniquePtr input,
        std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
            callback) override;

    void getHash(bcos::protocol::BlockNumber number,
        std::function<void(bcos::Error::UniquePtr, crypto::HashType)> callback) override;

    /* ----- XA Transaction interface Start ----- */

    // Write data to storage uncommitted
    void prepare(
        const TwoPCParams& params, std::function<void(bcos::Error::Ptr)> callback) override;

    // Commit uncommitted data
    void commit(const TwoPCParams& params, std::function<void(bcos::Error::Ptr)> callback) override;

    // Rollback the changes
    void rollback(
        const TwoPCParams& params, std::function<void(bcos::Error::Ptr)> callback) override;

    /* ----- XA Transaction interface End ----- */

    // drop all status
    void reset(std::function<void(bcos::Error::Ptr)> callback) override;

    std::shared_ptr<std::vector<std::string>> getTxCriticals(
        const protocol::Transaction::ConstPtr& _tx);

private:
    std::shared_ptr<BlockContext> createBlockContext(
        const protocol::BlockHeader::ConstPtr& currentHeader,
        storage::StateStorage::Ptr tableFactory);

    std::shared_ptr<BlockContext> createBlockContext(bcos::protocol::BlockNumber blockNumber,
        h256 blockHash, uint64_t timestamp, int32_t blockVersion,
        storage::StateStorage::Ptr tableFactory);

    std::shared_ptr<TransactionExecutive> createExecutive(
        const std::shared_ptr<BlockContext>& _blockContext, const std::string& _contractAddress,
        int64_t contextID, int64_t seq);

    void asyncExecute(std::shared_ptr<BlockContext> blockContext,
        bcos::protocol::ExecutionMessage::UniquePtr input, bool staticCall,
        std::function<void(bcos::Error::UniquePtr&&, bcos::protocol::ExecutionMessage::UniquePtr&&)>
            callback);

    void externalCall(std::shared_ptr<BlockContext> blockContext,
        std::shared_ptr<TransactionExecutive> executive,
        std::unique_ptr<CallParameters> callResults,
        std::function<void(Error::UniquePtr, std::unique_ptr<CallParameters>)> callback);

    std::unique_ptr<CallParameters> createCallParameters(
        bcos::protocol::ExecutionMessage&& inputs, bool staticCall);

    std::unique_ptr<CallParameters> createCallParameters(
        bcos::protocol::ExecutionMessage&& input, bcos::protocol::Transaction::Ptr&& tx);

    std::optional<std::vector<bcos::bytes>> decodeConflictFields(
        const FunctionAbi& functionAbi, bcos::protocol::Transaction* transaction);

    void initPrecompiled();

    void checkAndClear();

    void dagExecuteTransactionsForEvm(gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,
        bcos::protocol::TransactionsPtr transactions,
        std::function<void(
            bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
            callback);

    void dagExecuteTransactionsForWasm(
        gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,
        bcos::protocol::TransactionsPtr transactions,
        std::function<void(
            bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
            callback);

    txpool::TxPoolInterface::Ptr m_txpool;
    storage::MergeableStorageInterface::Ptr m_cachedStorage;
    std::shared_ptr<storage::TransactionalStorageInterface> m_backendStorage;
    protocol::ExecutionMessageFactory::Ptr m_executionMessageFactory;
    std::shared_ptr<BlockContext> m_blockContext;
    crypto::Hash::Ptr m_hashImpl;
    bool m_isWasm = false;
    const ExecutorVersion m_version;
    std::shared_ptr<ClockCache<bcos::bytes, FunctionAbi>> m_abiCache;

    struct State
    {
        State(bcos::protocol::BlockNumber _number, bcos::storage::StateStorage::Ptr _storage)
          : number(_number), storage(_storage)
        {}
        State(const State&) = delete;
        State& operator=(const State&) = delete;

        bcos::protocol::BlockNumber number;
        bcos::storage::StateStorage::Ptr storage;
    };
    std::list<State> m_stateStorages;
    bcos::protocol::BlockNumber m_lastCommitedBlockNumber = 1;

    struct HashCombine
    {
        size_t hash(const std::tuple<int64_t, int64_t>& val) const
        {
            size_t seed = hashInt64(std::get<0>(val));
            boost::hash_combine(seed, hashInt64(std::get<1>(val)));

            return seed;
        }

        bool equal(
            const std::tuple<int64_t, int64_t>& lhs, const std::tuple<int64_t, int64_t>& rhs) const
        {
            return std::get<0>(lhs) == std::get<0>(rhs) && std::get<1>(lhs) == std::get<1>(rhs);
        }

        std::hash<int64_t> hashInt64;
    };

    struct CallState
    {
        std::shared_ptr<BlockContext> blockContext;
    };
    tbb::concurrent_hash_map<std::tuple<int64_t, int64_t>, CallState, HashCombine> m_calledContext;
    std::shared_mutex m_stateStoragesMutex;

    std::shared_ptr<std::map<std::string, std::shared_ptr<PrecompiledContract>>>
        m_precompiledContract;
    std::map<std::string, std::shared_ptr<precompiled::Precompiled>> m_constantPrecompiled;
    std::shared_ptr<const std::set<std::string>> m_builtInPrecompiled;
    unsigned int m_threadNum = std::max(std::thread::hardware_concurrency(), (unsigned int)1);
};

}  // namespace executor
}  // namespace bcos
