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

#include "bcos-framework/interfaces/executor/ExecutionParams.h"
#include "bcos-framework/interfaces/executor/ExecutionResult.h"
#include "bcos-framework/interfaces/executor/ParallelTransactionExecutorInterface.h"
#include "bcos-framework/interfaces/protocol/Block.h"
#include "bcos-framework/interfaces/protocol/BlockFactory.h"
#include "bcos-framework/interfaces/protocol/Transaction.h"
#include "bcos-framework/interfaces/protocol/TransactionReceipt.h"
#include "bcos-framework/interfaces/storage/StorageInterface.h"
#include "bcos-framework/interfaces/txpool/TxPoolInterface.h"
#include "bcos-framework/libstorage/StateStorage.h"
#include "interfaces/crypto/Hash.h"
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
struct CallParameters;

using executionCallback =
    std::function<void(const Error::ConstPtr&, std::vector<protocol::ExecutionResult::Ptr>&)>;

class TransactionExecutor : public ParallelTransactionExecutorInterface,
                            public std::enable_shared_from_this<TransactionExecutor>
{
public:
    using Ptr = std::shared_ptr<TransactionExecutor>;
    using ConstPtr = std::shared_ptr<const TransactionExecutor>;
    using CallBackFunction = std::function<crypto::HashType(protocol::BlockNumber x)>;

    TransactionExecutor(txpool::TxPoolInterface::Ptr txpool,
        storage::TransactionalStorageInterface::Ptr backendStorage,
        protocol::ExecutionResultFactory::Ptr executionResultFactory,
        bcos::crypto::Hash::Ptr hashImpl, bool isWasm, size_t poolSize = 2);

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
        std::function<void(
            bcos::Error::Ptr&&, std::vector<std::tuple<std::string, crypto::HashType>>&&)>
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
        const bcos::protocol::ExecutionParams::ConstPtr& input);
    std::shared_ptr<BlockContext> createBlockContext(
        const protocol::BlockHeader::ConstPtr& currentHeader,
        storage::StateStorage::Ptr tableFactory);

    void asyncExecute(const bcos::protocol::ExecutionParams::ConstPtr& input, bool staticCall,
        std::function<void(bcos::Error::Ptr&&, bcos::protocol::ExecutionResult::Ptr&&)> callback);

    void onCallResultsCallback(std::shared_ptr<TransactionExecutive> executive,
        std::shared_ptr<CallParameters>&& callResults);

    std::string newEVMAddress(
        const std::string_view& sender, int64_t blockNumber, int64_t contextID);
    std::string newEVMAddress(
        const std::string_view& _sender, bytesConstRef _init, u256 const& _salt);

    txpool::TxPoolInterface::Ptr m_txpool;
    std::shared_ptr<storage::TransactionalStorageInterface> m_backendStorage;
    protocol::ExecutionResultFactory::Ptr m_executionResultFactory;
    std::shared_ptr<BlockContext> m_blockContext = nullptr;
    crypto::Hash::Ptr m_hashImpl;
    bool m_isWasm = false;
    const ExecutorVersion m_version;

    std::list<bcos::storage::StateStorage::Ptr> m_stateStorages;  // TODO: need lock to deal with
                                                                  // nextBlock and prepare?

    std::list<bcos::storage::StateStorage::Ptr>::const_iterator
        m_lastUncommitedIterator;  // last uncommited storage

    std::shared_ptr<ThreadPool> m_threadPool = nullptr;
    std::shared_ptr<std::map<std::string, std::shared_ptr<PrecompiledContract>>>
        m_precompiledContract;
};

}  // namespace executor

}  // namespace bcos
