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
 * @file TransactionExecutor.cpp
 * @author: xingqiangbai
 * @date: 2021-09-01
 */

#include "bcos-executor/TransactionExecutor.h"
#include "../ChecksumAddress.h"
#include "../Common.h"
#include "../executive/BlockContext.h"
#include "../executive/TransactionExecutive.h"
#include "../precompiled/CNSPrecompiled.h"
#include "../precompiled/CRUDPrecompiled.h"
#include "../precompiled/Common.h"
#include "../precompiled/ConsensusPrecompiled.h"
#include "../precompiled/CryptoPrecompiled.h"
#include "../precompiled/DeployWasmPrecompiled.h"
#include "../precompiled/FileSystemPrecompiled.h"
#include "../precompiled/KVTableFactoryPrecompiled.h"
#include "../precompiled/ParallelConfigPrecompiled.h"
#include "../precompiled/PrecompiledResult.h"
#include "../precompiled/SystemConfigPrecompiled.h"
#include "../precompiled/TableFactoryPrecompiled.h"
#include "../precompiled/Utilities.h"
#include "../precompiled/extension/DagTransferPrecompiled.h"
#include "../precompiled/extension/ContractAuthPrecompiled.h"
#include "../vm/Precompiled.h"
#include "Abi.h"
#include "ClockCache.h"
#include "ScaleUtils.h"
#include "TxDAG.h"
#include "bcos-framework/interfaces/dispatcher/SchedulerInterface.h"
#include "bcos-framework/interfaces/executor/PrecompiledTypeDef.h"
#include "bcos-framework/interfaces/ledger/LedgerTypeDef.h"
#include "bcos-framework/interfaces/protocol/TransactionReceipt.h"
#include "bcos-framework/interfaces/storage/Table.h"
#include "bcos-framework/libcodec/abi/ContractABIType.h"
#include "bcos-framework/libstorage/StateStorage.h"
#include "bcos-framework/libutilities/Error.h"
#include "bcos-framework/libutilities/ThreadPool.h"
#include "interfaces/executor/ExecutionMessage.h"
#include "interfaces/storage/StorageInterface.h"
#include "libprotocol/LogEntry.h"
#include "tbb/flow_graph.h"
#include <tbb/parallel_for.h>
#include <boost/algorithm/hex.hpp>
#include <boost/exception/detail/exception_ptr.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/throw_exception.hpp>
#include <cassert>
#include <exception>
#include <functional>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

using namespace bcos;
using namespace std;
using namespace bcos::executor;
using namespace bcos::protocol;
using namespace bcos::storage;
using namespace bcos::precompiled;
using namespace tbb::flow;

crypto::Hash::Ptr GlobalHashImpl::g_hashImpl;

TransactionExecutor::TransactionExecutor(txpool::TxPoolInterface::Ptr txpool,
    storage::TransactionalStorageInterface::Ptr backendStorage,
    protocol::ExecutionMessageFactory::Ptr executionMessageFactory,
    bcos::crypto::Hash::Ptr hashImpl, bool isWasm)
  : m_txpool(std::move(txpool)),
    m_backendStorage(std::move(backendStorage)),
    m_executionMessageFactory(std::move(executionMessageFactory)),
    m_hashImpl(std::move(hashImpl)),
    m_isWasm(isWasm),
    m_version(Version_3_0_0)  // current executor version, will set as new block's version
{
    assert(m_backendStorage);
    auto fillZero = [](int _num) -> std::string {
        std::stringstream stream;
        stream << std::setfill('0') << std::setw(40) << std::hex << _num;
        return stream.str();
    };

    m_lastUncommittedIterator = m_stateStorages.begin();

    m_precompiledContract =
        std::make_shared<std::map<std::string, std::shared_ptr<PrecompiledContract>>>();
    m_precompiledContract->insert(std::make_pair(fillZero(1),
        make_shared<PrecompiledContract>(3000, 0, PrecompiledRegistrar::executor("ecrecover"))));
    m_precompiledContract->insert(std::make_pair(fillZero(2),
        make_shared<PrecompiledContract>(60, 12, PrecompiledRegistrar::executor("sha256"))));
    m_precompiledContract->insert(std::make_pair(fillZero(3),
        make_shared<PrecompiledContract>(600, 120, PrecompiledRegistrar::executor("ripemd160"))));
    m_precompiledContract->insert(std::make_pair(fillZero(4),
        make_shared<PrecompiledContract>(15, 3, PrecompiledRegistrar::executor("identity"))));
    m_precompiledContract->insert(
        {fillZero(5), make_shared<PrecompiledContract>(PrecompiledRegistrar::pricer("modexp"),
                          PrecompiledRegistrar::executor("modexp"))});
    m_precompiledContract->insert(
        {fillZero(6), make_shared<PrecompiledContract>(
                          150, 0, PrecompiledRegistrar::executor("alt_bn128_G1_add"))});
    m_precompiledContract->insert(
        {fillZero(7), make_shared<PrecompiledContract>(
                          6000, 0, PrecompiledRegistrar::executor("alt_bn128_G1_mul"))});
    m_precompiledContract->insert({fillZero(8),
        make_shared<PrecompiledContract>(PrecompiledRegistrar::pricer("alt_bn128_pairing_product"),
            PrecompiledRegistrar::executor("alt_bn128_pairing_product"))});
    m_precompiledContract->insert({fillZero(9),
        make_shared<PrecompiledContract>(PrecompiledRegistrar::pricer("blake2_compression"),
            PrecompiledRegistrar::executor("blake2_compression"))});

    GlobalHashImpl::g_hashImpl = m_hashImpl;
    m_abiCache = make_shared<ClockCache<bcos::bytes, FunctionAbi>>(32);
}

void TransactionExecutor::nextBlockHeader(const bcos::protocol::BlockHeader::ConstPtr& blockHeader,
    std::function<void(bcos::Error::UniquePtr&&)> callback) noexcept
{
    try
    {
        EXECUTOR_LOG(INFO) << "NextBlockHeader request: "
                           << LOG_KV("number", blockHeader->number());

        bcos::storage::StateStorage::Ptr stateStorage;
        if (m_stateStorages.empty())
        {
            stateStorage = std::make_shared<bcos::storage::StateStorage>(m_backendStorage);
        }
        else
        {
            auto prev = m_stateStorages.back();
            stateStorage = std::make_shared<bcos::storage::StateStorage>(prev.storage);
        }

        m_blockContext = createBlockContext(blockHeader, stateStorage);
        m_stateStorages.push_back({blockHeader->number(), std::move(stateStorage)});

        if (m_lastUncommittedIterator == m_stateStorages.end())
        {
            m_lastUncommittedIterator = m_stateStorages.cend();
            --m_lastUncommittedIterator;
        }

        EXECUTOR_LOG(INFO) << "NextBlockHeader success";
        callback(nullptr);
    }
    catch (std::exception& e)
    {
        EXECUTOR_LOG(ERROR) << "NextBlockHeader error: " << boost::diagnostic_information(e);

        callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, "nextBlockHeader unknown error", e));
    }
}

void TransactionExecutor::dagExecuteTransactions(
    const gsl::span<bcos::protocol::ExecutionMessage::UniquePtr>& inputs,
    std::function<void(
        bcos::Error::UniquePtr&&, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>&&)>
        callback) noexcept
{
    auto txHashes = make_shared<HashList>(inputs.size());
    for (auto& execution_params : inputs)
    {
        assert(execution_params->type() == ExecutionMessage::TXHASH);
        txHashes->emplace_back(execution_params->transactionHash());
    }

    // TODO: After passing function testing, change sync behaviour to async way.
    std::promise<protocol::TransactionsPtr> promise;
    m_txpool->asyncFillBlock(txHashes, [&promise](Error::Ptr error, protocol::TransactionsPtr txs) {
        if (error)
        {
            EXECUTOR_LOG(ERROR) << LOG_BADGE("executor") << LOG_DESC("asyncFillBlock failed")
                                << LOG_KV("message", error->errorMessage());
            promise.set_exception(std::make_exception_ptr(error));
        }
        else
        {
            promise.set_value(txs);
        }
    });

    auto future = promise.get_future();
    TransactionsPtr transactions;
    try
    {
        transactions = future.get();
    }
    catch (exception_ptr error)
    {
        try
        {
            rethrow_exception(error);
        }
        catch (Error::UniquePtr& error)
        {
            callback(std::move(error), vector<ExecutionMessage::UniquePtr>());
            return;
        }
    }

    auto transactionsNum = transactions->size();
    auto executionResults = vector<ExecutionMessage::UniquePtr>(transactionsNum);
    auto allConflictFields = vector<optional<ConflictFields>>(transactionsNum, nullopt);

    tbb::parallel_for(tbb::blocked_range<uint64_t>(0, transactionsNum),
        [&](const tbb::blocked_range<uint64_t>& range) {
            for (auto i = range.begin(); i != range.end(); ++i)
            {
                auto defaultExecutionResult = m_executionMessageFactory->createExecutionMessage();
                executionResults[i].swap(defaultExecutionResult);

                auto transaction = transactions->at(i).get();
                auto to = transaction->to();
                auto input = transaction->input();
                auto selector = input.getCroppedData(0, 4);
                auto abiKey = bytes(to.cbegin(), to.cend());
                abiKey.insert(abiKey.end(), selector.begin(), selector.end());

                auto cacheHandle = m_abiCache->lookup(abiKey);
                optional<ConflictFields> conflictFields = nullopt;
                if (!cacheHandle.isValid())
                {
                    auto storage = m_blockContext->storage();
                    auto table = storage->openTable(to);
                    auto abiStr = table->getRow(ACCOUNT_ABI)->getField(SYS_VALUE);

                    auto functionAbi =
                        FunctionAbi::deserialize(abiStr, selector.toBytes(), m_hashImpl);
                    if (!functionAbi)
                    {
                        executionResults[i]->setType(ExecutionMessage::SEND_BACK);
                        continue;
                    }

                    auto abiPtr = functionAbi.get();
                    if (m_abiCache->insert(abiKey, abiPtr, &cacheHandle))
                    {
                        functionAbi.release();
                    }
                    conflictFields = decodeConflictFields(*abiPtr, transaction);
                }
                else
                {
                    auto& functionAbi = cacheHandle.value();
                    conflictFields = decodeConflictFields(functionAbi, transaction);
                }

                if (!conflictFields.has_value())
                {
                    executionResults[i]->setType(ExecutionMessage::SEND_BACK);
                    continue;
                }
                allConflictFields[i] = std::move(conflictFields);
            }
        });

    using Task = continue_node<continue_msg>;
    using Msg = const continue_msg&;

    auto tasks = vector<Task>();
    tasks.reserve(transactionsNum);
    auto flowGraph = graph();
    broadcast_node<continue_msg> start(flowGraph);

    auto dependencies = unordered_map<bytes, size_t, boost::hash<bytes>>();
    auto slotUsage = unordered_map<size_t, size_t>();

    for (auto i = 0u; i < allConflictFields.size(); ++i)
    {
        auto& conflictFields = allConflictFields[i];
        if (!conflictFields.has_value())
        {
            continue;
        }

        auto index = tasks.size();
        tasks.emplace_back(
            Task(flowGraph, [this, i, &inputs, &transactions, &executionResults](Msg) {
                auto& input = inputs[i];
                auto callParameters = createCallParameters(*input, std::move(transactions->at(i)));

                auto executive = std::make_shared<TransactionExecutive>(m_blockContext,
                    callParameters->codeAddress, input->contextID(), input->seq(),
                    std::bind(&TransactionExecutor::externalCall, this, std::placeholders::_1,
                        std::placeholders::_2, std::placeholders::_3));

                auto response = executive->execute(std::move(callParameters));
                executionResults[i]->setNewEVMContractAddress(response->newEVMContractAddress);
                executionResults[i]->setLogEntries(response->logEntries);
                executionResults[i]->setStatus(response->status);
                executionResults[i]->setMessage(response->message);
                if (response->status != 0)
                {
                    executionResults[i]->setType(ExecutionMessage::REVERT);
                }
                else
                {
                    executionResults[i]->setType(ExecutionMessage::FINISHED);
                }
            }));

        auto noDeps = true;
        for (auto& conflictField : conflictFields.value())
        {
            assert(conflictField.size() >= sizeof(size_t));

            auto slot = *(size_t*)conflictField.data();
            if (conflictField.size() != sizeof(size_t))
            {
                auto iter = dependencies.find(conflictField);
                if (iter != dependencies.end())
                {
                    noDeps = false;
                    make_edge(tasks[iter->second], tasks[index]);
                }
                dependencies[conflictField] = index;
            }

            auto iter = slotUsage.find(slot);
            if (iter != slotUsage.end())
            {
                noDeps = false;
                make_edge(tasks[iter->second], tasks[index]);
            }
            slotUsage[slot] = index;
        }

        if (noDeps)
        {
            make_edge(start, tasks[index]);
        }
    }

    start.try_put(continue_msg());
    flowGraph.wait_for_all();
    callback(nullptr, std::move(executionResults));
}

void TransactionExecutor::call(bcos::protocol::ExecutionMessage::UniquePtr input,
    std::function<void(bcos::Error::UniquePtr&&, bcos::protocol::ExecutionMessage::UniquePtr&&)>
        callback) noexcept
{
    EXECUTOR_LOG(INFO) << "Call request";
    asyncExecute(std::move(input), true,
        [callback = std::move(callback)](
            Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            if (error)
            {
                std::string errorMessage = "Call failed: " + boost::diagnostic_information(*error);
                EXECUTOR_LOG(ERROR) << errorMessage;
                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, errorMessage, *error), nullptr);
                return;
            }

            EXECUTOR_LOG(INFO) << "Call success";
            callback(std::move(error), std::move(result));
        });
}

void TransactionExecutor::executeTransaction(bcos::protocol::ExecutionMessage::UniquePtr input,
    std::function<void(bcos::Error::UniquePtr&&, bcos::protocol::ExecutionMessage::UniquePtr&&)>
        callback) noexcept
{
    EXECUTOR_LOG(INFO) << "ExecuteTransaction request" << LOG_KV("ContextID", input->contextID())
                       << LOG_KV("seq", input->seq()) << LOG_KV("Message type", input->type());
    asyncExecute(std::move(input), false,
        [callback = std::move(callback)](
            Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            if (error)
            {
                std::string errorMessage =
                    "ExecuteTransaction failed: " + boost::diagnostic_information(*error);
                EXECUTOR_LOG(ERROR) << errorMessage;
                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, errorMessage, *error), nullptr);
                return;
            }

            EXECUTOR_LOG(INFO) << "ExecuteTransaction success";
            callback(std::move(error), std::move(result));
        });
}

void TransactionExecutor::getTableHashes(bcos::protocol::BlockNumber number,
    std::function<void(
        bcos::Error::UniquePtr&&, std::vector<std::tuple<std::string, crypto::HashType>>&&)>
        callback) noexcept
{
    (void)callback;

    EXECUTOR_LOG(INFO) << "GetTableHashes" << LOG_KV("number", number);
    // if (m_stateStorages.empty())
    // {
    //     EXECUTOR_LOG(ERROR) << "GetTableHashes error: No uncommited state in executor";
    //     callback(BCOS_ERROR_PTR(-1, "No uncommited state in executor"),
    //         std::vector<std::tuple<std::string, crypto::HashType>>());
    //     return;
    // }

    // auto last = m_stateStorages.front();
    // if (last->blockNumber() != number)
    // {
    //     auto errorMessage = "GetTableHashes error: Request block number: " +
    //                         boost::lexical_cast<std::string>(number) +
    //                         " not equal to last blockNumber: " +
    //                         boost::lexical_cast<std::string>(last->blockNumber());

    //     EXECUTOR_LOG(ERROR) << errorMessage;
    //     callback(BCOS_ERROR_PTR(-1, errorMessage),
    //         std::vector<std::tuple<std::string, crypto::HashType>>());

    //     return;
    // }

    // auto tableHashes = last->tableHashes();
    // EXECUTOR_LOG(INFO) << "GetTableHashes success" << LOG_KV("size", tableHashes.size());

    // callback(nullptr, std::move(tableHashes));
}

void TransactionExecutor::prepare(
    const TwoPCParams& params, std::function<void(bcos::Error::Ptr&&)> callback) noexcept
{
    EXECUTOR_LOG(INFO) << "Prepare request" << LOG_KV("params", params.number);
    if (m_stateStorages.empty())
    {
        EXECUTOR_LOG(ERROR) << "Prepare error: No uncommited state in executor";
        callback(BCOS_ERROR_PTR(-1, "No uncommited state in executor"));
        return;
    }

    auto last = m_lastUncommittedIterator;
    if (last == m_stateStorages.end())
    {
        auto errorMessage = "Prepare error: empty stateStorages";
        EXECUTOR_LOG(ERROR) << errorMessage;
        callback(BCOS_ERROR_PTR(-1, errorMessage));

        return;
    }

    if (last->number != params.number)
    {
        auto errorMessage =
            "Prepare error: Request block number: " +
            boost::lexical_cast<std::string>(params.number) +
            " not equal to last blockNumber: " + boost::lexical_cast<std::string>(last->number);

        EXECUTOR_LOG(ERROR) << errorMessage;
        callback(BCOS_ERROR_PTR(-1, errorMessage));

        return;
    }

    bcos::storage::TransactionalStorageInterface::TwoPCParams storageParams;
    storageParams.number = params.number;
    m_backendStorage->asyncPrepare(
        storageParams, last->storage, [callback = std::move(callback)](auto&& error, uint64_t) {
            if (error)
            {
                auto errorMessage = "Prepare error: " + boost::diagnostic_information(*error);

                EXECUTOR_LOG(ERROR) << errorMessage;
                callback(BCOS_ERROR_WITH_PREV_PTR(-1, errorMessage, *error));
                return;
            }

            EXECUTOR_LOG(INFO) << "Prepare success";
            callback(nullptr);
        });
}

void TransactionExecutor::commit(
    const TwoPCParams& params, std::function<void(bcos::Error::Ptr&&)> callback) noexcept
{
    EXECUTOR_LOG(INFO) << "Commit request" << LOG_KV("number", params.number);

    if (m_lastUncommittedIterator == m_stateStorages.end())
    {
        EXECUTOR_LOG(ERROR) << "Commit error: No uncommited state in executor";
        callback(BCOS_ERROR_PTR(-1, "No uncommited state in executor"));
        return;
    }

    auto last = *m_lastUncommittedIterator;
    if (last.number != params.number)
    {
        auto errorMessage =
            "Commit error: Request block number: " +
            boost::lexical_cast<std::string>(params.number) +
            " not equal to last blockNumber: " + boost::lexical_cast<std::string>(last.number);

        EXECUTOR_LOG(ERROR) << errorMessage;
        callback(BCOS_ERROR_PTR(-1, errorMessage));

        return;
    }

    bcos::storage::TransactionalStorageInterface::TwoPCParams storageParams;
    storageParams.number = params.number;
    m_backendStorage->asyncCommit(storageParams,
        [this, callback = std::move(callback), it = m_stateStorages.begin()](Error::Ptr&& error) {
            if (error)
            {
                auto errorMessage = "Commit error: " + boost::diagnostic_information(*error);

                EXECUTOR_LOG(ERROR) << errorMessage;
                callback(BCOS_ERROR_WITH_PREV_PTR(-1, errorMessage, *error));
                return;
            }

            EXECUTOR_LOG(INFO) << "Commit success";

            ++m_lastUncommittedIterator;
            m_blockContext = nullptr;

            callback(nullptr);
        });
}

void TransactionExecutor::rollback(
    const TwoPCParams& params, std::function<void(bcos::Error::Ptr&&)> callback) noexcept
{
    EXECUTOR_LOG(INFO) << "Rollback request: " << LOG_KV("number", params.number);

    if (m_lastUncommittedIterator == m_stateStorages.end())
    {
        EXECUTOR_LOG(ERROR) << "Rollback error: No uncommited state in executor";
        callback(BCOS_ERROR_PTR(-1, "No uncommited state in executor"));
        return;
    }

    auto last = *m_lastUncommittedIterator;
    if (last.number != params.number)
    {
        auto errorMessage =
            "Rollback error: Request block number: " +
            boost::lexical_cast<std::string>(params.number) +
            " not equal to last blockNumber: " + boost::lexical_cast<std::string>(last.number);

        EXECUTOR_LOG(ERROR) << errorMessage;
        callback(BCOS_ERROR_PTR(-1, errorMessage));

        return;
    }

    bcos::storage::TransactionalStorageInterface::TwoPCParams storageParams;
    storageParams.number = params.number;
    m_backendStorage->asyncRollback(storageParams, [callback = std::move(callback)](auto&& error) {
        if (error)
        {
            auto errorMessage = "Rollback error: " + boost::diagnostic_information(*error);

            EXECUTOR_LOG(ERROR) << errorMessage;
            callback(BCOS_ERROR_WITH_PREV_PTR(-1, errorMessage, *error));
            return;
        }

        EXECUTOR_LOG(INFO) << "Rollback success";
        callback(nullptr);
    });
}

void TransactionExecutor::reset(std::function<void(bcos::Error::Ptr&&)> callback) noexcept
{
    callback(nullptr);
}

void TransactionExecutor::asyncExecute(bcos::protocol::ExecutionMessage::UniquePtr input,
    bool staticCall,
    std::function<void(bcos::Error::UniquePtr&&, bcos::protocol::ExecutionMessage::UniquePtr&&)>
        callback)
{
    std::shared_ptr<BlockContext> blockContext;

    if (staticCall)
    {
        // TODO: const call impl
    }
    else
    {
        if (!m_blockContext)
        {
            callback(BCOS_ERROR_UNIQUE_PTR(-1, "Execute failed with empty blockContext!"), nullptr);
            return;
        }

        blockContext = m_blockContext;
    }

    switch (input->type())
    {
    case bcos::protocol::ExecutionMessage::TXHASH:
    {
        // Get transaction first
        auto txHashes = std::make_shared<bcos::crypto::HashList>();
        txHashes->push_back(input->transactionHash());

        std::shared_ptr<bcos::protocol::ExecutionMessage> sharedInput = std::move(input);

        m_txpool->asyncFillBlock(std::move(txHashes), [this, input = std::move(sharedInput),
                                                          blockContext = std::move(blockContext),
                                                          callback](Error::Ptr error,
                                                          bcos::protocol::TransactionsPtr
                                                              transactions) {
            if (error)
            {
                callback(
                    BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1,
                        "Transaction does not exists: " + input->transactionHash().hex(), *error),
                    nullptr);
                return;
            }

            if (transactions->empty())
            {
                callback(BCOS_ERROR_UNIQUE_PTR(
                             -1, "Transaction does not exists: " + input->transactionHash().hex()),
                    nullptr);
                return;
            }

            auto tx = (*transactions)[0];

            auto callParameters = createCallParameters(*input, std::move(tx));

            auto executive = std::make_shared<TransactionExecutive>(blockContext,
                callParameters->codeAddress, input->contextID(), input->seq(),
                std::bind(&TransactionExecutor::externalCall, this, std::placeholders::_1,
                    std::placeholders::_2, std::placeholders::_3));

            blockContext->insertExecutive(input->contextID(), input->seq(), {executive, callback});

            try
            {
                executive->start(std::move(callParameters));
            }
            catch (std::exception& e)
            {
                EXECUTOR_LOG(ERROR) << "Execute error: " << boost::diagnostic_information(e);
                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, "Execute error", e), nullptr);
            }
        });
        break;
    }
    case bcos::protocol::ExecutionMessage::MESSAGE:
    case bcos::protocol::ExecutionMessage::REVERT:
    case bcos::protocol::ExecutionMessage::FINISHED:
    {
        auto callParameters = createCallParameters(*input, staticCall);

        auto it = blockContext->getExecutive(input->contextID(), input->seq());
        if (it)
        {
            // REVERT or FINISHED
            auto& [executive, externalCallFunc, responseFunc] = *it;
            externalCallFunc = std::move(callback);

            // Call callback
            EXECUTOR_LOG(TRACE) << "Entering responseFunc";
            responseFunc(nullptr, TransactionExecutive::CallMessage(std::move(callParameters)));
            EXECUTOR_LOG(TRACE) << "Exiting responseFunc";

            (void)executive;
        }
        else
        {
            // new external call MESSAGE
            auto executive = std::make_shared<TransactionExecutive>(blockContext,
                callParameters->codeAddress, input->contextID(), input->seq(),
                std::bind(&TransactionExecutor::externalCall, this, std::placeholders::_1,
                    std::placeholders::_2, std::placeholders::_3));

            blockContext->insertExecutive(input->contextID(), input->seq(), {executive, callback});

            try
            {
                executive->start(std::move(callParameters));
            }
            catch (std::exception& e)
            {
                EXECUTOR_LOG(ERROR) << "Execute error: " << boost::diagnostic_information(e);
                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, "Execute error", e), nullptr);
            }
        }

        break;
    }
    default:
    {
        EXECUTOR_LOG(ERROR) << "Unknown message type: " << input->type();
        callback(BCOS_ERROR_UNIQUE_PTR(
                     -1, "Unknown type" + boost::lexical_cast<std::string>(input->type())),
            nullptr);
        return;
    }
    }
}

optional<ConflictFields> TransactionExecutor::decodeConflictFields(
    const FunctionAbi& functionAbi, Transaction* transaction)
{
    if (functionAbi.conflictFields.empty())
    {
        return nullopt;
    }

    auto conflictFields = ConflictFields();
    auto to = transaction->to();
    auto hasher = boost::hash<string_view>();
    auto toHash = hasher(to);

    for (auto& conflictField : functionAbi.conflictFields)
    {
        auto key = bytes();
        size_t slot = toHash + conflictField.slot;
        auto slotBegin = (uint8_t*)static_cast<void*>(&slot);
        key.insert(key.end(), slotBegin, slotBegin + sizeof(slot));

        switch (conflictField.kind)
        {
        case All:
        case Len:
        {
            break;
        }
        case Env:
        {
            assert(conflictField.accessPath.size() == 1);
            auto envKind = conflictField.accessPath[0];
            switch (envKind)
            {
            case Caller:
            case Origin:
            {
                auto sender = transaction->sender();
                key.insert(key.end(), sender.begin(), sender.end());
                break;
            }
            case Now:
            {
                auto now = m_blockContext->timestamp();
                auto bytes = static_cast<byte*>(static_cast<void*>(&now));
                key.insert(key.end(), bytes, bytes + sizeof(now));
                break;
            }
            case BlockNumber:
            {
                auto blockNumber = m_blockContext->currentNumber();
                auto bytes = static_cast<byte*>(static_cast<void*>(&blockNumber));
                key.insert(key.end(), bytes, bytes + sizeof(blockNumber));
                break;
            }
            case Addr:
            {
                key.insert(key.end(), to.begin(), to.end());
                break;
            }
            default:
            {
                EXECUTOR_LOG(ERROR) << LOG_BADGE("unknown env kind in conflict field")
                                    << LOG_KV("envKind", envKind);
                return nullopt;
            }
            }
            break;
        }
        case Var:
        {
            assert(!conflictField.accessPath.empty());
            const ParameterAbi* paramAbi = nullptr;
            auto components = &functionAbi.inputs;
            auto inputData = transaction->input().getCroppedData(4).toBytes();

            auto startPos = 0u;
            for (auto segment : conflictField.accessPath)
            {
                if (segment >= components->size())
                {
                    return nullopt;
                }

                for (auto i = 0u; i < segment; ++i)
                {
                    auto length = scaleEncodingLength(components->at(i), inputData, startPos);
                    if (!length.has_value())
                    {
                        return nullopt;
                    }
                    startPos += length.value();
                }
                paramAbi = &components->at(segment);
                components = &paramAbi->components;
            }
            auto length = scaleEncodingLength(*paramAbi, inputData, startPos);
            if (!length.has_value())
            {
                return nullopt;
            }
            assert(startPos + length.value() <= inputData.size());
            key.insert(key.end(), inputData.begin() + startPos,
                inputData.begin() + startPos + length.value());
            break;
        }
        default:
        {
            EXECUTOR_LOG(ERROR) << LOG_BADGE("unknown conflict field kind")
                                << LOG_KV("conflictFieldKind", conflictField.kind);
            return nullopt;
        }
        }
        conflictFields.emplace_back(std::move(key));
    }
    return {conflictFields};
}

void TransactionExecutor::externalCall(TransactionExecutive::Ptr executive,
    std::unique_ptr<CallParameters> params,
    std::function<void(Error::UniquePtr, std::unique_ptr<CallParameters>)> callback)
{
    auto it = m_blockContext->getExecutive(executive->contextID(), executive->seq());
    if (!it)
    {
        BOOST_THROW_EXCEPTION(BCOS_ERROR(-1,
            "Can't find executive: " + boost::lexical_cast<std::string>(executive->contextID()) +
                "," + boost::lexical_cast<std::string>(executive->seq())));
    }

    if (callback)
    {
        std::get<2>(*it) = std::move(callback);
    }

    auto message = m_executionMessageFactory->createExecutionMessage();
    switch (params->type)
    {
    case CallParameters::MESSAGE:
        toChecksumAddress(params->senderAddress, m_hashImpl);
        toChecksumAddress(params->receiveAddress, m_hashImpl);

        message->setFrom(std::move(params->senderAddress));
        message->setTo(std::move(params->receiveAddress));
        message->setType(ExecutionMessage::MESSAGE);
        break;
    case CallParameters::WAIT_KEY:
        message->setType(ExecutionMessage::WAIT_KEY);
        break;
    case CallParameters::FINISHED:
        toChecksumAddress(params->senderAddress, m_hashImpl);
        toChecksumAddress(params->receiveAddress, m_hashImpl);

        // Response message, Swap the from and to
        message->setFrom(std::move(params->receiveAddress));
        message->setTo(std::move(params->senderAddress));
        message->setType(ExecutionMessage::FINISHED);
        break;
    case CallParameters::REVERT:
        toChecksumAddress(params->senderAddress, m_hashImpl);
        toChecksumAddress(params->receiveAddress, m_hashImpl);

        // Response message, Swap the from and to
        message->setFrom(std::move(params->receiveAddress));
        message->setTo(std::move(params->senderAddress));
        message->setType(ExecutionMessage::REVERT);
        break;
    }

    message->setContextID(executive->contextID());
    message->setSeq(executive->seq());

    message->setOrigin(std::move(params->origin));

    message->setGasAvailable(params->gas);
    message->setData(std::move(params->data));
    message->setStaticCall(params->staticCall);
    message->setCreate(params->create);
    if (params->createSalt)
    {
        message->setCreateSalt(*params->createSalt);
    }

    message->setStatus(params->status);
    message->setMessage(std::move(params->message));
    message->setLogEntries(std::move(params->logEntries));
    message->setNewEVMContractAddress(std::move(params->newEVMContractAddress));

    std::get<1> (*it)(nullptr, std::move(message));
}

BlockContext::Ptr TransactionExecutor::createBlockContext(
    const protocol::BlockHeader::ConstPtr& currentHeader, storage::StateStorage::Ptr tableFactory)
{
    (void)m_version;  // TODO: accord to m_version to chose schedule

    BlockContext::Ptr context = make_shared<BlockContext>(tableFactory, m_hashImpl, currentHeader,
        m_executionMessageFactory, FiscoBcosScheduleV3, m_isWasm);

    auto tableFactoryPrecompiled =
        std::make_shared<precompiled::TableFactoryPrecompiled>(m_hashImpl);
    tableFactoryPrecompiled->setMemoryTableFactory(tableFactory);
    auto sysConfig = std::make_shared<precompiled::SystemConfigPrecompiled>(m_hashImpl);
    auto parallelConfigPrecompiled =
        std::make_shared<precompiled::ParallelConfigPrecompiled>(m_hashImpl);
    auto consensusPrecompiled = std::make_shared<precompiled::ConsensusPrecompiled>(m_hashImpl);
    auto cnsPrecompiled = std::make_shared<precompiled::CNSPrecompiled>(m_hashImpl);
    auto kvTableFactoryPrecompiled =
        std::make_shared<precompiled::KVTableFactoryPrecompiled>(m_hashImpl);
    kvTableFactoryPrecompiled->setMemoryTableFactory(tableFactory);

    // FIXME: use more elegant way to switch wasm env
    if (m_isWasm)
    {
        context->setAddress2Precompiled(SYS_CONFIG_NAME, sysConfig);
        context->setAddress2Precompiled(TABLE_FACTORY_NAME, tableFactoryPrecompiled);
        context->setAddress2Precompiled(CONSENSUS_NAME, consensusPrecompiled);
        context->setAddress2Precompiled(CNS_NAME, cnsPrecompiled);
        context->setAddress2Precompiled(PARALLEL_CONFIG_NAME, parallelConfigPrecompiled);
        context->setAddress2Precompiled(KV_TABLE_FACTORY_NAME, kvTableFactoryPrecompiled);
        context->setAddress2Precompiled(
            CRYPTO_NAME, std::make_shared<precompiled::CryptoPrecompiled>(m_hashImpl));
        context->setAddress2Precompiled(
            DAG_TRANSFER_NAME, std::make_shared<precompiled::DagTransferPrecompiled>(m_hashImpl));
        context->setAddress2Precompiled(
            CRYPTO_NAME, std::make_shared<CryptoPrecompiled>(m_hashImpl));
        context->setAddress2Precompiled(
            DEPLOY_WASM_NAME, std::make_shared<DeployWasmPrecompiled>(m_hashImpl));
        context->setAddress2Precompiled(
            CRUD_NAME, std::make_shared<precompiled::CRUDPrecompiled>(m_hashImpl));
        context->setAddress2Precompiled(
            BFS_NAME, std::make_shared<precompiled::FileSystemPrecompiled>(m_hashImpl));
        // TODO: use unique address for ContractAuthPrecompiled
        context->setAddress2Precompiled(
            PERMISSION_NAME, std::make_shared<precompiled::ContractAuthPrecompiled>(m_hashImpl));
        vector<string> builtIn = {CRYPTO_NAME};
        context->setBuiltInPrecompiled(make_shared<vector<string>>(builtIn));
    }
    else
    {
        context->setAddress2Precompiled(SYS_CONFIG_ADDRESS, sysConfig);
        context->setAddress2Precompiled(TABLE_FACTORY_ADDRESS, tableFactoryPrecompiled);
        context->setAddress2Precompiled(CONSENSUS_ADDRESS, consensusPrecompiled);
        context->setAddress2Precompiled(CNS_ADDRESS, cnsPrecompiled);
        context->setAddress2Precompiled(PARALLEL_CONFIG_ADDRESS, parallelConfigPrecompiled);
        context->setAddress2Precompiled(KV_TABLE_FACTORY_ADDRESS, kvTableFactoryPrecompiled);
        context->setAddress2Precompiled(
            CRYPTO_ADDRESS, std::make_shared<precompiled::CryptoPrecompiled>(m_hashImpl));
        context->setAddress2Precompiled(DAG_TRANSFER_ADDRESS,
            std::make_shared<precompiled::DagTransferPrecompiled>(m_hashImpl));
        context->setAddress2Precompiled(
            CRYPTO_ADDRESS, std::make_shared<CryptoPrecompiled>(m_hashImpl));
        context->setAddress2Precompiled(
            DEPLOY_WASM_ADDRESS, std::make_shared<DeployWasmPrecompiled>(m_hashImpl));
        context->setAddress2Precompiled(
            CRUD_ADDRESS, std::make_shared<precompiled::CRUDPrecompiled>(m_hashImpl));
        context->setAddress2Precompiled(
            BFS_ADDRESS, std::make_shared<precompiled::FileSystemPrecompiled>(m_hashImpl));
        // TODO: use unique address for ContractAuthPrecompiled
         context->setAddress2Precompiled(
             PERMISSION_ADDRESS, std::make_shared<precompiled::ContractAuthPrecompiled>(m_hashImpl));
        vector<string> builtIn = {CRYPTO_ADDRESS};
        context->setBuiltInPrecompiled(make_shared<vector<string>>(builtIn));
    }
    // context->setAddress2Precompiled(
    //     PERMISSION_ADDRESS, std::make_shared<precompiled::PermissionPrecompiled>());
    // context->setAddress2Precompiled(
    // CONTRACT_LIFECYCLE_ADDRESS,
    // std::make_shared<precompiled::ContractLifeCyclePrecompiled>());
    // context->setAddress2Precompiled(
    //     CHAINGOVERNANCE_ADDRESS,
    //     std::make_shared<precompiled::ChainGovernancePrecompiled>());

    // TODO: register User developed Precompiled contract
    // registerUserPrecompiled(context);

    context->setPrecompiledContract(m_precompiledContract);

    // TODO: getTxGasLimitToContext from precompiled and set to context
    context->setTxCriticalsHandler([&](const protocol::Transaction::ConstPtr& _tx)
                                       -> std::shared_ptr<std::vector<std::string>> {
        if (_tx->type() == protocol::TransactionType::ContractCreation)
        {
            // Not to parallel contract creation transaction
            return nullptr;
        }

        auto p = context->getPrecompiled(string(_tx->to()));
        if (p)
        {
            // Precompile transaction
            if (p->isParallelPrecompiled())
            {
                auto ret = make_shared<vector<string>>(p->getParallelTag(_tx->input()));
                for (string& critical : *ret)
                {
                    critical += _tx->to();
                }
                return ret;
            }
            return nullptr;
        }
        uint32_t selector = precompiled::getParamFunc(_tx->input());

        auto receiveAddress = _tx->to();
        std::shared_ptr<precompiled::ParallelConfig> config = nullptr;
        // hit the cache, fetch ParallelConfig from the cache directly
        // Note: Only when initializing DAG, get ParallelConfig, will not get
        // during transaction execution
        // TODO: add parallel config cache
        // auto parallelKey = std::make_pair(string(receiveAddress), selector);
        config = parallelConfigPrecompiled->getParallelConfig(
            context, receiveAddress, selector, _tx->sender());

        if (config == nullptr)
        {
            return nullptr;
        }
        // Testing code
        auto res = make_shared<vector<string>>();

        codec::abi::ABIFunc af;
        bool isOk = af.parser(config->functionName);
        if (!isOk)
        {
            EXECUTOR_LOG(DEBUG) << LOG_DESC("[getTxCriticals] parser function signature failed, ")
                                << LOG_KV("func signature", config->functionName);

            return nullptr;
        }

        auto paramTypes = af.getParamsType();
        if (paramTypes.size() < (size_t)config->criticalSize)
        {
            EXECUTOR_LOG(DEBUG) << LOG_DESC("[getTxCriticals] params type less than  criticalSize")
                                << LOG_KV("func signature", config->functionName)
                                << LOG_KV("func criticalSize", config->criticalSize);

            return nullptr;
        }

        paramTypes.resize((size_t)config->criticalSize);

        codec::abi::ContractABICodec abi(m_hashImpl);
        isOk = abi.abiOutByFuncSelector(_tx->input().getCroppedData(4), paramTypes, *res);
        if (!isOk)
        {
            EXECUTOR_LOG(DEBUG) << LOG_DESC("[getTxCriticals] abiout failed, ")
                                << LOG_KV("func signature", config->functionName);

            return nullptr;
        }

        for (string& critical : *res)
        {
            critical += _tx->to();
        }

        return res;
    });
    return context;
}

std::string TransactionExecutor::newEVMAddress(
    const std::string_view& sender, int64_t blockNumber, int64_t contextID)
{
    auto hash =
        m_hashImpl->hash(std::string(sender) + boost::lexical_cast<std::string>(blockNumber) +
                         boost::lexical_cast<std::string>(contextID));

    std::string hexAddress;
    hexAddress.reserve(40);
    boost::algorithm::hex(hash.data(), hash.data() + 20, std::back_inserter(hexAddress));

    toChecksumAddress(hexAddress, m_hashImpl);

    return hexAddress;
}

std::string TransactionExecutor::newEVMAddress(
    const std::string_view& _sender, bytesConstRef _init, u256 const& _salt)
{
    auto hash = m_hashImpl->hash(
        bytes{0xff} + toBytes(_sender) + toBigEndian(_salt) + m_hashImpl->hash(_init));

    std::string hexAddress;
    hexAddress.reserve(40);
    boost::algorithm::hex(hash.data(), hash.data() + 20, std::back_inserter(hexAddress));

    toChecksumAddress(hexAddress, m_hashImpl);

    return hexAddress;
}

std::unique_ptr<CallParameters> TransactionExecutor::createCallParameters(
    const bcos::protocol::ExecutionMessage& input, bool staticCall)
{
    auto callParameters = std::make_unique<CallParameters>(CallParameters::MESSAGE);
    callParameters->origin = input.origin();
    callParameters->senderAddress = input.from();
    callParameters->receiveAddress = input.to();
    callParameters->codeAddress = input.to();
    callParameters->create = input.create();
    callParameters->gas = input.gasAvailable();
    callParameters->data = input.data().toBytes();
    callParameters->staticCall = staticCall;
    callParameters->create = input.create();
    callParameters->newEVMContractAddress = input.newEVMContractAddress();
    callParameters->status = 0;

    return callParameters;
}

std::unique_ptr<CallParameters> TransactionExecutor::createCallParameters(
    const bcos::protocol::ExecutionMessage& input, bcos::protocol::Transaction::Ptr&& tx)
{
    auto callParameters = std::make_unique<CallParameters>(CallParameters::MESSAGE);

    bool create = false;
    if (m_isWasm)
    {
        callParameters->origin = tx->sender();
        callParameters->senderAddress = tx->sender();
        callParameters->receiveAddress = tx->to();
        callParameters->codeAddress = callParameters->receiveAddress;
    }
    else
    {
        callParameters->origin.reserve(tx->sender().size() * 2);
        boost::algorithm::hex_lower(tx->sender(), std::back_inserter(callParameters->origin));
        toChecksumAddress(callParameters->origin, m_hashImpl);

        callParameters->senderAddress = callParameters->origin;
        callParameters->receiveAddress = input.to();
        callParameters->codeAddress = callParameters->receiveAddress;
    }

    callParameters->create = create;
    callParameters->gas = input.gasAvailable();
    callParameters->data = tx->input().toBytes();
    callParameters->staticCall = false;
    callParameters->create = input.create();

    return callParameters;
}