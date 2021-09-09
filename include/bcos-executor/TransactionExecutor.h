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

#include "bcos-framework/interfaces/dispatcher/SchedulerInterface.h"
#include "bcos-framework/interfaces/executor/ExecutionParams.h"
#include "bcos-framework/interfaces/executor/ExecutionResult.h"
#include "bcos-framework/interfaces/executor/ParallelTransactionExecutorInterface.h"
#include "bcos-framework/interfaces/ledger/LedgerInterface.h"
#include "bcos-framework/interfaces/protocol/Block.h"
#include "bcos-framework/interfaces/protocol/BlockFactory.h"
#include "bcos-framework/interfaces/protocol/Transaction.h"
#include "bcos-framework/interfaces/protocol/TransactionReceipt.h"
#include "bcos-framework/interfaces/storage/StorageInterface.h"
#include "bcos-framework/interfaces/txpool/TxPoolInterface.h"
#include "bcos-framework/libstorage/StateStorage.h"
#include "tbb/concurrent_unordered_map.h"
#include <boost/function.hpp>
#include <algorithm>
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

using executionCallback =
    std::function<void(const Error::ConstPtr&, std::vector<protocol::ExecutionResult::Ptr>&)>;

class TransactionExecutor : public ParallelTransactionExecutorInterface,
                            public std::enable_shared_from_this<TransactionExecutor>
{
public:
    using Ptr = std::shared_ptr<TransactionExecutor>;
    using ConstPtr = std::shared_ptr<const TransactionExecutor>;
    using CallBackFunction = std::function<crypto::HashType(protocol::BlockNumber x)>;
    TransactionExecutor(const protocol::BlockFactory::Ptr& _blockFactory,
        const std::shared_ptr<dispatcher::SchedulerInterface>& _scheduler,
        const ledger::LedgerInterface::Ptr& _ledger, const txpool::TxPoolInterface::Ptr& _txpool,
        const std::shared_ptr<storage::TransactionalStorageInterface>& _stateStorage,
        const std::shared_ptr<storage::MergeableStorageInterface>& _cacheStorage,
        protocol::ExecutionResultFactory::Ptr _executionResultFactory, bool _isWasm,
        size_t _poolSize = 2);

    virtual ~TransactionExecutor() { stop(); }

    void stop() {}
    void start() {}

    void nextBlockHeader(const bcos::protocol::BlockHeader::ConstPtr& blockHeader,
        std::function<void(bcos::Error::Ptr&&)> callback) noexcept override;

    void executeTransaction(const bcos::protocol::ExecutionParams::ConstPtr& inputs,
        std::function<void(bcos::Error::Ptr&&, bcos::protocol::ExecutionResult::Ptr&&)>
            callback) noexcept override;

    void dagExecuteTransactions(const gsl::span<bcos::protocol::ExecutionParams::ConstPtr>& inputs,
        std::function<void(bcos::Error::Ptr&&, std::vector<bcos::protocol::ExecutionResult::Ptr>&&)>
            callback) noexcept override;

    void call(const bcos::protocol::ExecutionParams::ConstPtr& input,
        std::function<void(bcos::Error::Ptr&&, bcos::protocol::ExecutionResult::Ptr&&)>
            callback) noexcept override;

    void getTableHashes(bcos::protocol::BlockNumber number,
        std::function<void(bcos::Error::Ptr&&, std::vector<std::tuple<std::string, crypto::HashType>>&&)> callback) noexcept
        override;

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

    void releaseCallContext(int64_t contextID) {
        // FIXME: need a lock?
        m_contextsOfCall.unsafe_erase(contextID);
    }
private:
    protocol::BlockHeader::Ptr getLatestHeaderFromStorage();
    protocol::BlockNumber getLatestBlockNumberFromStorage();
    protocol::Transaction::Ptr createTransaction(
        const bcos::protocol::ExecutionParams::ConstPtr& input);
    std::shared_ptr<BlockContext> createBlockContext(
        const protocol::BlockHeader::ConstPtr& currentHeader,
        storage::StateStorage::Ptr tableFactory);
    void asyncExecute(protocol::Transaction::ConstPtr transaction,
        std::shared_ptr<TransactionExecutive> executive, bool staticCall);

    protocol::BlockFactory::Ptr m_blockFactory;
    std::shared_ptr<dispatcher::SchedulerInterface> m_scheduler;
    ledger::LedgerInterface::Ptr m_ledger;
    txpool::TxPoolInterface::Ptr m_txpool;
    std::mutex m_tableStorageMutex;
    storage::StateStorage::Ptr m_tableStorage;
    std::shared_ptr<storage::TransactionalStorageInterface> m_backendStorage;
    std::shared_ptr<storage::MergeableStorageInterface> m_cacheStorage;
    protocol::ExecutionResultFactory::Ptr m_executionResultFactory;
    std::shared_ptr<BlockContext> m_blockContext = nullptr;
    std::shared_ptr<ThreadPool> m_threadPool = nullptr;
    CallBackFunction m_pNumberHash;
    crypto::Hash::Ptr m_hashImpl;
    unsigned int m_threadNum = -1;
    bool m_isWasm = false;
    std::atomic_bool m_stop = {false};
    std::shared_ptr<const protocol::BlockHeader> m_lastHeader = nullptr;
    std::unique_ptr<std::thread> m_worker = nullptr;
    const ExecutorVersion m_version;
    std::map<std::string, std::shared_ptr<PrecompiledContract>> m_precompiledContract;

    tbb::concurrent_unordered_map<int64_t, std::shared_ptr<BlockContext>> m_contextsOfCall;
    // TODO: add a lock of m_uncommittedData
    std::mutex m_uncommittedDataMutex;
    std::queue<storage::StateStorage::Ptr> m_uncommittedData;
};

}  // namespace executor

}  // namespace bcos
