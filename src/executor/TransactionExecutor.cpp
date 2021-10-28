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
#include "../Common.h"
#include "../dag/Abi.h"
#include "../dag/ClockCache.h"
#include "../dag/ScaleUtils.h"
#include "../dag/TxDAG.h"
#include "../executive/BlockContext.h"
#include "../executive/TransactionExecutive.h"
#include "../precompiled/CNSPrecompiled.h"
#include "../precompiled/CRUDPrecompiled.h"
#include "../precompiled/Common.h"
#include "../precompiled/ConsensusPrecompiled.h"
#include "../precompiled/CryptoPrecompiled.h"
#include "../precompiled/FileSystemPrecompiled.h"
#include "../precompiled/KVTableFactoryPrecompiled.h"
#include "../precompiled/ParallelConfigPrecompiled.h"
#include "../precompiled/PrecompiledResult.h"
#include "../precompiled/SystemConfigPrecompiled.h"
#include "../precompiled/TableFactoryPrecompiled.h"
#include "../precompiled/Utilities.h"
#include "../precompiled/extension/ContractAuthPrecompiled.h"
#include "../precompiled/extension/DagTransferPrecompiled.h"
#include "../vm/Precompiled.h"
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
#include "interfaces/protocol/ProtocolTypeDef.h"
#include "interfaces/storage/StorageInterface.h"
#include "libprotocol/LogEntry.h"
#include "tbb/flow_graph.h"
#include <tbb/parallel_for.h>
#include <boost/algorithm/hex.hpp>
#include <boost/exception/detail/exception_ptr.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/thread/latch.hpp>
#include <boost/throw_exception.hpp>
#include <cassert>
#include <exception>
#include <functional>
#include <iterator>
#include <mutex>
#include <shared_mutex>
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
    storage::MergeableStorageInterface::Ptr cachedStorage,
    storage::TransactionalStorageInterface::Ptr backendStorage,
    protocol::ExecutionMessageFactory::Ptr executionMessageFactory,
    bcos::crypto::Hash::Ptr hashImpl, bool isWasm)
  : m_txpool(std::move(txpool)),
    m_cachedStorage(std::move(cachedStorage)),
    m_backendStorage(std::move(backendStorage)),
    m_executionMessageFactory(std::move(executionMessageFactory)),
    m_hashImpl(std::move(hashImpl)),
    m_isWasm(isWasm),
    m_version(Version_3_0_0)  // current executor version, will set as new block's version
{
    assert(m_backendStorage);

    m_lastUncommittedIterator = m_stateStorages.begin();
    initPrecompiled();
    assert(m_precompiledContract);
    assert(!m_constantPrecompiled.empty());
    assert(m_builtInPrecompiled);
    GlobalHashImpl::g_hashImpl = m_hashImpl;
    m_abiCache = make_shared<ClockCache<bcos::bytes, FunctionAbi>>(32);
}

void TransactionExecutor::nextBlockHeader(const bcos::protocol::BlockHeader::ConstPtr& blockHeader,
    std::function<void(bcos::Error::UniquePtr)> callback)
{
    try
    {
        EXECUTOR_LOG(INFO) << "NextBlockHeader request: "
                           << LOG_KV("number", blockHeader->number());

        {
            std::unique_lock<std::shared_mutex> lock(m_stateStoragesMutex);
            bcos::storage::StateStorage::Ptr stateStorage;
            if (m_stateStorages.empty())
            {
                if (m_cachedStorage)
                {
                    stateStorage = std::make_shared<bcos::storage::StateStorage>(m_cachedStorage);
                }
                else
                {
                    stateStorage = std::make_shared<bcos::storage::StateStorage>(m_backendStorage);
                }
            }
            else
            {
                auto prev = m_stateStorages.back();
                stateStorage = std::make_shared<bcos::storage::StateStorage>(prev.storage);
            }


            m_blockContext = createBlockContext(blockHeader, stateStorage);
            m_stateStorages.emplace_back(State{blockHeader->number(), std::move(stateStorage)});

            if (m_lastUncommittedIterator == m_stateStorages.end())
            {
                m_lastUncommittedIterator = m_stateStorages.cend();
                --m_lastUncommittedIterator;
            }
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
    gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,
    std::function<void(
        bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
        callback)
{
    auto txHashes = make_shared<HashList>();
    txHashes->reserve(inputs.size());
    for (auto& execution_params : inputs)
    {
        assert(execution_params->type() == ExecutionMessage::TXHASH);
        txHashes->emplace_back(execution_params->transactionHash());
    }

    m_txpool->asyncFillBlock(
        txHashes, [&](Error::Ptr error, protocol::TransactionsPtr transactions) {
            if (error)
            {
                std::string errorMessage =
                    "asyncFillBlock failed: " + boost::diagnostic_information(*error);
                EXECUTOR_LOG(ERROR) << errorMessage;
                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, errorMessage, *error),
                    vector<ExecutionMessage::UniquePtr>());
            }
            else
            {
                if (m_isWasm)
                {
                    dagExecuteTransactionsForWasm(
                        std::move(inputs), std::move(transactions), std::move(callback));
                }
                else
                {
                    dagExecuteTransactionsForEvm(
                        std::move(inputs), std::move(transactions), std::move(callback));
                }
            }
        });
}

void TransactionExecutor::dagExecuteTransactionsForEvm(
    gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,
    bcos::protocol::TransactionsPtr transactions,
    std::function<void(
        bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
        callback)
{
    auto transactionsNum = transactions->size();
    vector<ExecutionMessage::UniquePtr> executionResults(transactionsNum);

    // get criticals
    std::vector<std::shared_ptr<std::vector<std::string>>> txsCriticals;
    txsCriticals.resize(transactionsNum);
    size_t serialTransactionsNum = 0;
    tbb::parallel_for(tbb::blocked_range<uint64_t>(0, transactionsNum),
        [&](const tbb::blocked_range<uint64_t>& range) {
            for (uint64_t i = range.begin(); i < range.end(); i++)
            {
                auto defaultExecutionResult = m_executionMessageFactory->createExecutionMessage();
                executionResults[i].swap(defaultExecutionResult);

                txsCriticals[i] = getTxCriticals(transactions->at(i));
                if (!txsCriticals[i])
                {
                    serialTransactionsNum++;
                    executionResults[i]->setType(ExecutionMessage::SEND_BACK);
                }
            }
        });

    shared_ptr<TxDAG> txDag = make_shared<TxDAG>();
    txDag->init(transactions, txsCriticals);
    boost::latch counter(transactionsNum - serialTransactionsNum);

    vector<TransactionExecutive::Ptr> allExecutives(transactionsNum);
    vector<std::unique_ptr<CallParameters>> allCallParameters(transactionsNum);

    for (size_t i = 0; i < transactionsNum; ++i)
    {
        if (!txsCriticals[i])
        {
            continue;
        }

        auto& input = inputs[i];
        auto contextID = input->contextID();
        auto seq = input->seq();
        auto callParameters =
            createCallParameters(std::move(*input), std::move(transactions->at(i)));

        auto executive =
            createExecutive(m_blockContext, callParameters->codeAddress, contextID, seq);
        m_blockContext->insertExecutive(contextID, seq,
            {executive,
                [i, &executionResults, &counter](bcos::Error::UniquePtr&& error,
                    bcos::protocol::ExecutionMessage::UniquePtr&& response) {
                    if (response->status() != 0 || error)
                    {
                        EXECUTOR_LOG(DEBUG) << "Transaction reverted";
                        executionResults[i]->setType(ExecutionMessage::REVERT);
                    }
                    else
                    {
                        EXECUTOR_LOG(DEBUG) << "Transaction executed";
                        executionResults[i]->setNewEVMContractAddress(
                            std::string(response->newEVMContractAddress()));
                        executionResults[i]->setLogEntries(response->takeLogEntries());
                        executionResults[i]->setStatus(response->status());
                        executionResults[i]->setMessage(std::string(response->message()));
                        executionResults[i]->setType(ExecutionMessage::FINISHED);
                        executionResults[i]->setData(response->takeData());
                        executionResults[i]->setTransactionHash(response->transactionHash());
                        executionResults[i]->setFrom(string(response->from()));
                        executionResults[i]->setTo(string(response->to()));
                        executionResults[i]->setGasAvailable(response->gasAvailable());
                    }
                    counter.count_down();
                },
                {}});

        allExecutives[i].swap(executive);
        allCallParameters[i].swap(callParameters);
    }

    txDag->setTxExecuteFunc([](bcos::executor::TransactionExecutive::Ptr executive,
                                CallParameters::UniquePtr callParameters) {
        try
        {
            executive->start(std::move(callParameters));
        }
        catch (std::exception& e)
        {
            EXECUTOR_LOG(ERROR) << "Execute error: " << boost::diagnostic_information(e);
        }
    });

    auto parallelTimeOut = utcSteadyTime() + 30000;  // 30 timeout
    try
    {
        tbb::atomic<bool> isWarnedTimeout(false);
        tbb::parallel_for(tbb::blocked_range<unsigned int>(0, m_threadNum),
            [&](const tbb::blocked_range<unsigned int>& _r) {
                (void)_r;

                while (!txDag->hasFinished())
                {
                    if (!isWarnedTimeout.load() && utcSteadyTime() >= parallelTimeOut)
                    {
                        isWarnedTimeout.store(true);
                        EXECUTOR_LOG(WARNING)
                            << LOG_BADGE("executeBlock") << LOG_DESC("Para execute block timeout")
                            << LOG_KV("txNum", transactions->size())
                            << LOG_KV("blockNumber", m_blockContext->number());
                    }
                    txDag->executeUnit(allExecutives, allCallParameters);
                }
            });
    }
    catch (exception& e)
    {
        EXECUTOR_LOG(ERROR) << LOG_BADGE("executeBlock")
                            << LOG_DESC("Error during parallel block execution")
                            << LOG_KV("EINFO", boost::diagnostic_information(e));
        callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::CALL_ERROR, boost::diagnostic_information(e)),
            vector<ExecutionMessage::UniquePtr>{});
        return;
    }

    counter.wait();
    callback(nullptr, std::move(executionResults));
}

void TransactionExecutor::dagExecuteTransactionsForWasm(
    gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,
    bcos::protocol::TransactionsPtr transactions,
    std::function<void(
        bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
        callback)
{
    auto transactionsNum = transactions->size();
    auto executionResults = vector<ExecutionMessage::UniquePtr>(transactionsNum);
    auto allConflictFields = vector<optional<ConflictFields>>(transactionsNum, nullopt);

    mutex tableMutex;
    tbb::parallel_for(tbb::blocked_range<uint64_t>(0, transactionsNum),
        [&](const tbb::blocked_range<uint64_t>& range) {
            for (auto i = range.begin(); i != range.end(); ++i)
            {
                auto defaultExecutionResult = m_executionMessageFactory->createExecutionMessage();
                executionResults[i].swap(defaultExecutionResult);

                auto transaction = transactions->at(i).get();
                assert(transaction);

                auto to = transaction->to();
                auto input = transaction->input();
                auto selector = input.getCroppedData(0, 4);
                auto abiKey = bytes(to.cbegin(), to.cend());
                abiKey.insert(abiKey.end(), selector.begin(), selector.end());

                auto cacheHandle = m_abiCache->lookup(abiKey);
                optional<ConflictFields> conflictFields = nullopt;
                if (!cacheHandle.isValid())
                {
                    EXECUTOR_LOG(DEBUG) << LOG_BADGE("dagExecuteTransactionsForWasm")
                                        << LOG_DESC("No ABI found in cache, try to load")
                                        << LOG_KV("abiKey", toHexStringWithPrefix(abiKey));

                    std::lock_guard guard(tableMutex);

                    cacheHandle = m_abiCache->lookup(abiKey);
                    if (cacheHandle.isValid())
                    {
                        EXECUTOR_LOG(DEBUG) << LOG_BADGE("dagExecuteTransactionsForWasm")
                                            << LOG_DESC("ABI had beed loaded by other workers")
                                            << LOG_KV("abiKey", toHexStringWithPrefix(abiKey));
                        auto& functionAbi = cacheHandle.value();
                        conflictFields = decodeConflictFields(functionAbi, transaction);
                    }
                    else
                    {
                        auto storage = m_blockContext->storage();
                        auto tableName = "/apps" + string(to);
                        auto table = storage->openTable(tableName);
                        assert(table.has_value());

                        auto abiStr = table->getRow(ACCOUNT_ABI)->getField(SYS_VALUE);

                        EXECUTOR_LOG(DEBUG) << LOG_BADGE("dagExecuteTransactionsForWasm")
                                            << LOG_DESC("ABI loaded") << LOG_KV("ABI", abiStr);

                        auto functionAbi =
                            FunctionAbi::deserialize(abiStr, selector.toBytes(), m_hashImpl);
                        if (!functionAbi)
                        {
                            executionResults[i]->setType(ExecutionMessage::SEND_BACK);
                            // If abi is not valid, we don't impact the cache. In such a
                            // situation, if the caller invokes this method over and over
                            // again, executor will read the contract table repeatedly,
                            // which may cause performance loss. But we think occurrence
                            // of invalid abi is impossible in actual situations.
                            continue;
                        }

                        auto abiPtr = functionAbi.get();
                        if (m_abiCache->insert(abiKey, abiPtr, &cacheHandle))
                        {
                            // If abi object had been inserted into the cache successfully,
                            // the cache will take charge of life time management of the
                            // object. After this object being eliminated, the cache will
                            // delete its memory storage.
                            functionAbi.release();
                        }
                        conflictFields = decodeConflictFields(*abiPtr, transaction);
                    }
                }
                else
                {
                    EXECUTOR_LOG(DEBUG) << LOG_BADGE("dagExecuteTransactionsForWasm")
                                        << LOG_DESC("Found ABI in cache")
                                        << LOG_KV("abiKey", toHexStringWithPrefix(abiKey));
                    auto& functionAbi = cacheHandle.value();
                    conflictFields = decodeConflictFields(functionAbi, transaction);
                }

                if (!conflictFields.has_value())
                {
                    EXECUTOR_LOG(DEBUG)
                        << LOG_BADGE("dagExecuteTransactionsForWasm")
                        << LOG_DESC("The transaction can't be executed concurrently")
                        << LOG_KV("abiKey", toHexStringWithPrefix(abiKey));
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

    auto dependencies = unordered_map<bytes, vector<size_t>, boost::hash<bytes>>();
    auto slotUsage = unordered_map<size_t, size_t>();

    boost::latch counter(allConflictFields.size());

    for (auto i = 0u; i < allConflictFields.size(); ++i)
    {
        auto& conflictFields = allConflictFields[i];
        if (!conflictFields.has_value())
        {
            // Transactions those invokes method which can't be executed concurrently
            // will be sent back.
            counter.count_down();
            continue;
        }

        auto& input = inputs[i];
        auto contextID = input->contextID();
        auto seq = input->seq();

        auto executive = createExecutive(m_blockContext, string(input->to()), contextID, seq);
        m_blockContext->insertExecutive(contextID, seq,
            {executive,
                [i, &executionResults, &counter](bcos::Error::UniquePtr&& error,
                    bcos::protocol::ExecutionMessage::UniquePtr&& response) {
                    if (response->status() != 0 || error)
                    {
                        EXECUTOR_LOG(DEBUG) << "Transaction reverted";
                        executionResults[i]->setType(ExecutionMessage::REVERT);
                    }
                    else
                    {
                        EXECUTOR_LOG(DEBUG) << "Transaction executed";
                        executionResults[i]->setNewEVMContractAddress(
                            std::string(response->newEVMContractAddress()));
                        executionResults[i]->setLogEntries(response->takeLogEntries());
                        executionResults[i]->setStatus(response->status());
                        executionResults[i]->setMessage(std::string(response->message()));
                        executionResults[i]->setType(ExecutionMessage::FINISHED);
                        executionResults[i]->setData(response->takeData());
                        executionResults[i]->setTransactionHash(response->transactionHash());
                        executionResults[i]->setFrom(string(response->from()));
                        executionResults[i]->setTo(string(response->to()));
                        executionResults[i]->setGasAvailable(response->gasAvailable());
                    }
                    counter.count_down();
                },
                {}});

        auto task = [this, i, executive, &inputs, &transactions, &executionResults](Msg) {
            EXECUTOR_LOG(DEBUG) << LOG_BADGE("dagExecuteTransactionsForWasm")
                                << LOG_DESC("Start transaction") << LOG_KV("to", inputs[i]->to())
                                << LOG_KV("contextID", inputs[i]->contextID())
                                << LOG_KV("seq", inputs[i]->seq());
            auto callParameters =
                createCallParameters(std::move(*inputs[i]), std::move(transactions->at(i)));
            try
            {
                executive->start(std::move(callParameters));
            }
            catch (std::exception& e)
            {
                EXECUTOR_LOG(ERROR) << "Execute error: " << boost::diagnostic_information(e);
                executionResults[i]->setType(ExecutionMessage::REVERT);
            }
        };
        auto index = tasks.size();
        auto t = Task(flowGraph, std::move(task));
        tasks.push_back(t);

        auto noDeps = true;
        for (auto& conflictField : conflictFields.value())
        {
            assert(conflictField.size() >= sizeof(size_t));

            auto slot = *(size_t*)conflictField.data();
            auto iter = slotUsage.find(slot);
            if (iter != slotUsage.end() && iter->second != index)
            {
                noDeps = false;
                make_edge(tasks[iter->second], tasks[index]);

                EXECUTOR_LOG(DEBUG) << LOG_BADGE("dagExecuteTransactionsForWasm")
                                    << LOG_DESC("Make dependency for slot")
                                    << LOG_KV("from", iter->second) << LOG_KV("to", index);
            }

            if (conflictField.size() != sizeof(size_t))
            {
                auto iter = dependencies.find(conflictField);
                if (iter != dependencies.end() && iter->second.back() != index)
                {
                    noDeps = false;
                    make_edge(tasks[iter->second.back()], tasks[index]);

                    EXECUTOR_LOG(DEBUG)
                        << LOG_BADGE("dagExecuteTransactionsForWasm")
                        << LOG_DESC("Make dependency for key")
                        << LOG_KV("from", iter->second.back()) << LOG_KV("to", index);
                    dependencies[conflictField].push_back(index);
                }
                else
                {
                    dependencies[conflictField] = {index};
                }
            }
            else
            {
                // `slotUsage` is used when some a conflict key equals to `All` or `Len`. In such
                // cases, if there are 2 transations and one of them uses all of slot 0  meanwhile
                // another use a key to visit slot 0, then their conflict keys may looks very
                // different, and they can't be captured by `dependencies` only.
                for (auto& slotIndices : dependencies)
                {
                    auto prevSlot = *(size_t*)slotIndices.first.data();
                    if (prevSlot == slot)
                    {
                        auto& prevIndices = slotIndices.second;
                        if (prevIndices.size() != 0)
                        {
                            noDeps = false;
                        }
                        for (auto prevIndex : prevIndices)
                        {
                            make_edge(tasks[prevIndex], tasks[index]);
                        }
                    }
                }
                slotUsage[slot] = index;
            }
        }

        if (noDeps)
        {
            make_edge(start, tasks[index]);
            EXECUTOR_LOG(DEBUG) << LOG_BADGE("dagExecuteTransactionsForWasm")
                                << LOG_DESC("Make dependency for start") << LOG_KV("from", "start")
                                << LOG_KV("to", index);
        }
    }

    start.try_put(continue_msg());
    flowGraph.wait_for_all();
    counter.wait();
    callback(nullptr, std::move(executionResults));
}

void TransactionExecutor::call(bcos::protocol::ExecutionMessage::UniquePtr input,
    std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
        callback)
{
    EXECUTOR_LOG(DEBUG) << "Call request" << LOG_KV("ContextID", input->contextID())
                        << LOG_KV("seq", input->seq()) << LOG_KV("Message type", input->type())
                        << LOG_KV("To", input->to()) << LOG_KV("Create", input->create());

    BlockContext::Ptr blockContext;
    switch (input->type())
    {
    case protocol::ExecutionMessage::MESSAGE:
    {
        storage::StorageInterface::Ptr prev;
        bcos::protocol::BlockNumber number;
        {
            std::shared_lock<std::shared_mutex> lock(m_stateStoragesMutex);

            if (m_stateStorages.empty())
            {
                prev = m_backendStorage;
                number = m_lastCommitedBlockNumber;
            }
            else
            {
                auto& state = m_stateStorages.back();
                prev = state.storage;
                number = state.number;
            }
        }

        // Create a temp storage
        auto storage = std::make_shared<storage::StateStorage>(std::move(prev));

        // Create a temp block context
        blockContext = createBlockContext(
            number, h256(), 0, 0, std::move(storage));  // TODO: complete the block info
        auto inserted = m_calledContext.emplace(
            std::tuple{input->contextID(), input->seq()}, CallState{blockContext});

        if (!inserted)
        {
            auto message =
                "Call error, contextID: " + boost::lexical_cast<std::string>(input->contextID()) +
                " seq: " + boost::lexical_cast<std::string>(input->seq()) + " exists";
            EXECUTOR_LOG(ERROR) << message;
            callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::CALL_ERROR, message), nullptr);
            return;
        }

        break;
    }
    case protocol::ExecutionMessage::FINISHED:
    case protocol::ExecutionMessage::REVERT:
    {
        decltype(m_calledContext)::accessor it;
        m_calledContext.find(it, std::tuple{input->contextID(), input->seq()});

        if (it.empty())
        {
            auto message =
                "Call error, contextID: " + boost::lexical_cast<std::string>(input->contextID()) +
                " seq: " + boost::lexical_cast<std::string>(input->seq()) + " does not exists";
            EXECUTOR_LOG(ERROR) << message;
            callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::CALL_ERROR, message), nullptr);
            return;
        }

        blockContext = it->second.blockContext;

        break;
    }
    default:
    {
        auto message =
            "Call error, Unknown call type: " + boost::lexical_cast<std::string>(input->type());
        EXECUTOR_LOG(ERROR) << message;
        callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::CALL_ERROR, message), nullptr);
        return;

        break;
    }
    }

    asyncExecute(std::move(blockContext), std::move(input), true,
        [this, callback = std::move(callback)](
            Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            if (error)
            {
                std::string errorMessage = "Call failed: " + boost::diagnostic_information(*error);
                EXECUTOR_LOG(ERROR) << errorMessage;
                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, errorMessage, *error), nullptr);
                return;
            }

            if (result->type() == protocol::ExecutionMessage::FINISHED ||
                result->type() == protocol::ExecutionMessage::REVERT)
            {
                auto erased = m_calledContext.erase(std::tuple{result->contextID(), result->seq()});

                if (!erased)
                {
                    auto message = "Call error, erase contextID: " +
                                   boost::lexical_cast<std::string>(result->contextID()) +
                                   " seq: " + boost::lexical_cast<std::string>(result->seq()) +
                                   " does not exists";
                    EXECUTOR_LOG(ERROR) << message;

                    callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::CALL_ERROR, message), nullptr);
                    return;
                }
            }

            EXECUTOR_LOG(DEBUG) << "Call success";
            callback(std::move(error), std::move(result));
        });
}

void TransactionExecutor::executeTransaction(bcos::protocol::ExecutionMessage::UniquePtr input,
    std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
        callback)
{
    EXECUTOR_LOG(TRACE) << "ExecuteTransaction request" << LOG_KV("ContextID", input->contextID())
                        << LOG_KV("seq", input->seq()) << LOG_KV("Message type", input->type())
                        << LOG_KV("To", input->to()) << LOG_KV("Create", input->create());

    if (!m_blockContext)
    {
        callback(BCOS_ERROR_UNIQUE_PTR(
                     ExecuteError::EXECUTE_ERROR, "Execute failed with empty blockContext!"),
            nullptr);
        return;
    }

    asyncExecute(m_blockContext, std::move(input), false,
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

            // EXECUTOR_LOG(DEBUG) << "ExecuteTransaction success";
            callback(std::move(error), std::move(result));
        });
}

void TransactionExecutor::getHash(bcos::protocol::BlockNumber number,
    std::function<void(bcos::Error::UniquePtr, crypto::HashType)> callback)
{
    EXECUTOR_LOG(INFO) << "GetTableHashes" << LOG_KV("number", number);

    if (m_stateStorages.empty())
    {
        EXECUTOR_LOG(ERROR) << "GetTableHashes error: No uncommitted state";
        callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::GETHASH_ERROR, "No uncommitted state"),
            crypto::HashType());
        return;
    }

    auto last = m_stateStorages.back();
    if (last.number != number)
    {
        auto errorMessage =
            "GetTableHashes error: Request block number: " +
            boost::lexical_cast<std::string>(number) +
            " not equal to last blockNumber: " + boost::lexical_cast<std::string>(last.number);

        EXECUTOR_LOG(ERROR) << errorMessage;
        callback(
            BCOS_ERROR_UNIQUE_PTR(ExecuteError::GETHASH_ERROR, errorMessage), crypto::HashType());

        return;
    }

    auto hash = last.storage->hash(m_hashImpl);
    EXECUTOR_LOG(INFO) << "GetTableHashes success" << LOG_KV("hash", hash.hex());

    callback(nullptr, std::move(hash));
}

void TransactionExecutor::prepare(
    const TwoPCParams& params, std::function<void(bcos::Error::Ptr)> callback)
{
    EXECUTOR_LOG(INFO) << "Prepare request" << LOG_KV("params", params.number);
    if (m_stateStorages.empty())
    {
        EXECUTOR_LOG(ERROR) << "Prepare error: No uncommitted state in executor";
        callback(BCOS_ERROR_PTR(-1, "No uncommitted state in executor"));
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
        callback(BCOS_ERROR_PTR(ExecuteError::PREPARE_ERROR, errorMessage));

        return;
    }

    bcos::storage::TransactionalStorageInterface::TwoPCParams storageParams;  // TODO: add tikv
                                                                              // params
    storageParams.number = params.number;

    m_backendStorage->asyncPrepare(
        storageParams, last->storage, [callback = std::move(callback)](auto&& error, uint64_t) {
            if (error)
            {
                auto errorMessage = "Prepare error: " + boost::diagnostic_information(*error);

                EXECUTOR_LOG(ERROR) << errorMessage;
                callback(
                    BCOS_ERROR_WITH_PREV_PTR(ExecuteError::PREPARE_ERROR, errorMessage, *error));
                return;
            }

            EXECUTOR_LOG(INFO) << "Prepare success";
            callback(nullptr);
        });
}

void TransactionExecutor::commit(
    const TwoPCParams& params, std::function<void(bcos::Error::Ptr)> callback)
{
    EXECUTOR_LOG(DEBUG) << "Commit request" << LOG_KV("number", params.number);

    if (m_lastUncommittedIterator == m_stateStorages.end())
    {
        EXECUTOR_LOG(ERROR) << "Commit error: No uncommited state in executor";
        callback(BCOS_ERROR_PTR(ExecuteError::COMMIT_ERROR, "No uncommited state in executor"));
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

    bcos::storage::TransactionalStorageInterface::TwoPCParams storageParams;  // Add tikv params
    storageParams.number = params.number;
    m_backendStorage->asyncCommit(storageParams,
        [this, callback = std::move(callback), blockNumber = params.number](Error::Ptr&& error) {
            if (error)
            {
                auto errorMessage = "Commit error: " + boost::diagnostic_information(*error);

                EXECUTOR_LOG(ERROR) << errorMessage;
                callback(
                    BCOS_ERROR_WITH_PREV_PTR(ExecuteError::COMMIT_ERROR, errorMessage, *error));
                return;
            }

            EXECUTOR_LOG(DEBUG) << "Commit success";

            ++m_lastUncommittedIterator;
            m_lastCommitedBlockNumber = blockNumber;

            checkAndClear();

            callback(nullptr);
        });
}

void TransactionExecutor::rollback(
    const TwoPCParams& params, std::function<void(bcos::Error::Ptr)> callback)
{
    EXECUTOR_LOG(INFO) << "Rollback request: " << LOG_KV("number", params.number);

    if (m_lastUncommittedIterator == m_stateStorages.end())
    {
        EXECUTOR_LOG(ERROR) << "Rollback error: No uncommited state in executor";
        callback(BCOS_ERROR_PTR(ExecuteError::ROLLBACK_ERROR, "No uncommited state in executor"));
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
        callback(BCOS_ERROR_PTR(ExecuteError::ROLLBACK_ERROR, errorMessage));

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

void TransactionExecutor::reset(std::function<void(bcos::Error::Ptr)> callback)
{
    m_stateStorages.clear();
    m_lastUncommittedIterator = m_stateStorages.end();

    callback(nullptr);
}

void TransactionExecutor::asyncExecute(std::shared_ptr<BlockContext> blockContext,
    bcos::protocol::ExecutionMessage::UniquePtr input, bool staticCall,
    std::function<void(bcos::Error::UniquePtr&&, bcos::protocol::ExecutionMessage::UniquePtr&&)>
        callback)
{
    switch (input->type())
    {
    case bcos::protocol::ExecutionMessage::TXHASH:
    {
        // Get transaction first
        auto txHashes = std::make_shared<bcos::crypto::HashList>(1);
        (*txHashes)[0] = (input->transactionHash());

        std::shared_ptr<bcos::protocol::ExecutionMessage> sharedInput(std::move(input));

        m_txpool->asyncFillBlock(std::move(txHashes),
            [this, input = std::move(sharedInput), blockContext = std::move(blockContext),
                callback](Error::Ptr error, bcos::protocol::TransactionsPtr transactions) {
                if (error)
                {
                    callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(ExecuteError::EXECUTE_ERROR,
                                 "Transaction does not exists: " + input->transactionHash().hex(),
                                 *error),
                        nullptr);
                    return;
                }

                if (!transactions || transactions->empty())
                {
                    callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::EXECUTE_ERROR,
                                 "Transaction does not exists: " + input->transactionHash().hex()),
                        nullptr);
                    return;
                }

                auto tx = (*transactions)[0];
                if (!tx)
                {
                    callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::EXECUTE_ERROR,
                                 "Transaction is null: " + input->transactionHash().hex()),
                        nullptr);
                    return;
                }

                auto contextID = input->contextID();
                auto seq = input->seq();
                auto callParameters = createCallParameters(std::move(*input), std::move(tx));

                auto executive =
                    createExecutive(blockContext, callParameters->codeAddress, contextID, seq);

                blockContext->insertExecutive(contextID, seq, {executive, callback, {}});

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
        auto contextID = input->contextID();
        auto seq = input->seq();
        auto callParameters = createCallParameters(std::move(*input), staticCall);

        auto it = blockContext->getExecutive(contextID, seq);
        if (it)
        {
            // REVERT or FINISHED
            [[maybe_unused]] auto& [executive, externalCallFunc, responseFunc] = *it;
            it->requestFunction = std::move(callback);

            // Call callback
            EXECUTOR_LOG(TRACE) << "Entering responseFunc";
            responseFunc(nullptr, TransactionExecutive::CallMessage(std::move(callParameters)));
            EXECUTOR_LOG(TRACE) << "Exiting responseFunc";
        }
        else
        {
            // new external call MESSAGE
            auto executive =
                createExecutive(blockContext, callParameters->codeAddress, contextID, seq);

            blockContext->insertExecutive(contextID, seq, {executive, callback, {}});

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
        callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::EXECUTE_ERROR,
                     "Unknown type" + boost::lexical_cast<std::string>(input->type())),
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

        EXECUTOR_LOG(DEBUG) << LOG_BADGE("decodeConflictFields") << LOG_KV("to", to)
                            << LOG_KV("functionName", functionAbi.name)
                            << LOG_KV("slot", (int)conflictField.slot);

        switch (conflictField.kind)
        {
        case All:
        {
            EXECUTOR_LOG(DEBUG) << LOG_BADGE("decodeConflictFields") << LOG_DESC("use `All`");
            break;
        }
        case Len:
        {
            EXECUTOR_LOG(DEBUG) << LOG_BADGE("decodeConflictFields") << LOG_DESC("use `Len`");
            break;
        }
        case Env:
        {
            assert(conflictField.accessPath.size() == 1);

            auto envKind = conflictField.accessPath[0];
            switch (envKind)
            {
            case Caller:
            {
                auto sender = transaction->sender();
                key.insert(key.end(), sender.begin(), sender.end());

                EXECUTOR_LOG(DEBUG) << LOG_BADGE("decodeConflictFields") << LOG_DESC("use `Caller`")
                                    << LOG_KV("caller", sender);
                break;
            }
            case Origin:
            {
                auto sender = transaction->sender();
                key.insert(key.end(), sender.begin(), sender.end());

                EXECUTOR_LOG(DEBUG) << LOG_BADGE("decodeConflictFields") << LOG_DESC("use `Origin`")
                                    << LOG_KV("origin", sender);
                break;
            }
            case Now:
            {
                auto now = m_blockContext->timestamp();
                auto bytes = static_cast<byte*>(static_cast<void*>(&now));
                key.insert(key.end(), bytes, bytes + sizeof(now));

                EXECUTOR_LOG(DEBUG) << LOG_BADGE("decodeConflictFields") << LOG_DESC("use `Now`")
                                    << LOG_KV("now", now);
                break;
            }
            case BlockNumber:
            {
                auto blockNumber = m_blockContext->number();
                auto bytes = static_cast<byte*>(static_cast<void*>(&blockNumber));
                key.insert(key.end(), bytes, bytes + sizeof(blockNumber));

                EXECUTOR_LOG(DEBUG)
                    << LOG_BADGE("decodeConflictFields") << LOG_DESC("use `BlockNumber`")
                    << LOG_KV("functionName", functionAbi.name)
                    << LOG_KV("blockNumber", blockNumber);
                break;
            }
            case Addr:
            {
                key.insert(key.end(), to.begin(), to.end());

                EXECUTOR_LOG(DEBUG) << LOG_BADGE("decodeConflictFields") << LOG_DESC("use `Addr`")
                                    << LOG_KV("addr", to);
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
            bytes var(inputData.begin() + startPos, inputData.begin() + startPos + length.value());
            key.insert(key.end(), var.begin(), var.end());


            EXECUTOR_LOG(DEBUG) << LOG_BADGE("decodeConflictFields") << LOG_DESC("use `Var`")
                                << LOG_KV("functionName", functionAbi.name)
                                << LOG_KV("var", toHexStringWithPrefix(var));
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

void TransactionExecutor::externalCall(std::shared_ptr<BlockContext> blockContext,
    TransactionExecutive::Ptr executive, std::unique_ptr<CallParameters> params,
    std::function<void(Error::UniquePtr, std::unique_ptr<CallParameters>)> callback)
{
    auto it = blockContext->getExecutive(executive->contextID(), executive->seq());
    if (!it)
    {
        BOOST_THROW_EXCEPTION(BCOS_ERROR(-1,
            "Can't find executive: " + boost::lexical_cast<std::string>(executive->contextID()) +
                "," + boost::lexical_cast<std::string>(executive->seq())));
    }

    if (callback)
    {
        it->responseFunction = std::move(callback);
    }

    auto message = m_executionMessageFactory->createExecutionMessage();
    switch (params->type)
    {
    case CallParameters::MESSAGE:
        message->setFrom(std::move(params->senderAddress));
        message->setTo(std::move(params->receiveAddress));
        message->setType(ExecutionMessage::MESSAGE);
        break;
    case CallParameters::WAIT_KEY:
        message->setFrom(std::move(params->senderAddress));
        message->setType(ExecutionMessage::WAIT_KEY);
        message->setKeyLockAcquired(std::move(params->acquireKeyLock));

        break;
    case CallParameters::FINISHED:
        // Response message, Swap the from and to
        message->setFrom(std::move(params->receiveAddress));
        message->setTo(std::move(params->senderAddress));
        message->setType(ExecutionMessage::FINISHED);
        break;
    case CallParameters::REVERT:
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

    message->setKeyLocks(std::move(params->keyLocks));

    it->requestFunction(nullptr, std::move(message));
}

BlockContext::Ptr TransactionExecutor::createBlockContext(
    const protocol::BlockHeader::ConstPtr& currentHeader, storage::StateStorage::Ptr storage)
{
    BlockContext::Ptr context = make_shared<BlockContext>(
        storage, m_hashImpl, currentHeader, FiscoBcosScheduleV3, m_isWasm);

    return context;
}

std::shared_ptr<BlockContext> TransactionExecutor::createBlockContext(
    bcos::protocol::BlockNumber blockNumber, h256 blockHash, uint64_t timestamp,
    int32_t blockVersion, storage::StateStorage::Ptr storage)
{
    BlockContext::Ptr context = make_shared<BlockContext>(storage, m_hashImpl, blockNumber,
        blockHash, timestamp, blockVersion, FiscoBcosScheduleV3, m_isWasm);

    return context;
}

TransactionExecutive::Ptr TransactionExecutor::createExecutive(
    const std::shared_ptr<BlockContext>& _blockContext, const std::string& _contractAddress,
    int64_t contextID, int64_t seq)
{
    auto executive =
        std::make_shared<TransactionExecutive>(_blockContext, _contractAddress, contextID, seq,
            std::bind(&TransactionExecutor::externalCall, this, std::placeholders::_1,
                std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));

    executive->setConstantPrecompiled(m_constantPrecompiled);
    executive->setEVMPrecompiled(m_precompiledContract);
    executive->setBuiltInPrecompiled(m_builtInPrecompiled);
    // TODO: register User developed Precompiled contract
    // registerUserPrecompiled(context);
    return executive;
}

void TransactionExecutor::initPrecompiled()
{
    auto fillZero = [](int _num) -> std::string {
        std::stringstream stream;
        stream << std::setfill('0') << std::setw(40) << std::hex << _num;
        return stream.str();
    };
    m_precompiledContract =
        std::make_shared<std::map<std::string, std::shared_ptr<PrecompiledContract>>>();
    m_builtInPrecompiled = std::make_shared<std::set<std::string>>();

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

    auto sysConfig = std::make_shared<precompiled::SystemConfigPrecompiled>(m_hashImpl);
    auto parallelConfigPrecompiled =
        std::make_shared<precompiled::ParallelConfigPrecompiled>(m_hashImpl);
    auto consensusPrecompiled = std::make_shared<precompiled::ConsensusPrecompiled>(m_hashImpl);
    auto cnsPrecompiled = std::make_shared<precompiled::CNSPrecompiled>(m_hashImpl);
    auto tableFactoryPrecompiled =
        std::make_shared<precompiled::TableFactoryPrecompiled>(m_hashImpl);
    auto kvTableFactoryPrecompiled =
        std::make_shared<precompiled::KVTableFactoryPrecompiled>(m_hashImpl);

    if (m_isWasm)
    {
        m_constantPrecompiled.insert({SYS_CONFIG_NAME, sysConfig});
        m_constantPrecompiled.insert({CONSENSUS_NAME, consensusPrecompiled});
        m_constantPrecompiled.insert({CNS_NAME, cnsPrecompiled});
        m_constantPrecompiled.insert({PARALLEL_CONFIG_NAME, parallelConfigPrecompiled});
        m_constantPrecompiled.insert({TABLE_FACTORY_NAME, tableFactoryPrecompiled});
        m_constantPrecompiled.insert({KV_TABLE_FACTORY_NAME, kvTableFactoryPrecompiled});
        m_constantPrecompiled.insert(
            {DAG_TRANSFER_NAME, std::make_shared<precompiled::DagTransferPrecompiled>(m_hashImpl)});
        m_constantPrecompiled.insert(
            {CRYPTO_NAME, std::make_shared<CryptoPrecompiled>(m_hashImpl)});
        m_constantPrecompiled.insert(
            {CRUD_NAME, std::make_shared<precompiled::CRUDPrecompiled>(m_hashImpl)});
        m_constantPrecompiled.insert(
            {BFS_NAME, std::make_shared<precompiled::FileSystemPrecompiled>(m_hashImpl)});
        // TODO: use unique address for ContractAuthPrecompiled
        m_constantPrecompiled.insert(
            {PERMISSION_NAME, std::make_shared<precompiled::ContractAuthPrecompiled>(m_hashImpl)});

        set<string> builtIn = {CRYPTO_NAME};
        m_builtInPrecompiled = make_shared<set<string>>(builtIn);
    }
    else
    {
        m_constantPrecompiled.insert({SYS_CONFIG_ADDRESS, sysConfig});
        m_constantPrecompiled.insert({CONSENSUS_ADDRESS, consensusPrecompiled});
        m_constantPrecompiled.insert({CNS_ADDRESS, cnsPrecompiled});
        m_constantPrecompiled.insert({PARALLEL_CONFIG_ADDRESS, parallelConfigPrecompiled});
        m_constantPrecompiled.insert({TABLE_FACTORY_ADDRESS, tableFactoryPrecompiled});
        m_constantPrecompiled.insert({KV_TABLE_FACTORY_ADDRESS, kvTableFactoryPrecompiled});
        m_constantPrecompiled.insert({DAG_TRANSFER_ADDRESS,
            std::make_shared<precompiled::DagTransferPrecompiled>(m_hashImpl)});
        m_constantPrecompiled.insert(
            {CRYPTO_ADDRESS, std::make_shared<CryptoPrecompiled>(m_hashImpl)});
        m_constantPrecompiled.insert(
            {CRUD_ADDRESS, std::make_shared<precompiled::CRUDPrecompiled>(m_hashImpl)});
        m_constantPrecompiled.insert(
            {BFS_ADDRESS, std::make_shared<precompiled::FileSystemPrecompiled>(m_hashImpl)});
        // TODO: use unique address for ContractAuthPrecompiled
        m_constantPrecompiled.insert({PERMISSION_ADDRESS,
            std::make_shared<precompiled::ContractAuthPrecompiled>(m_hashImpl)});
        set<string> builtIn = {CRYPTO_ADDRESS};
        m_builtInPrecompiled = make_shared<set<string>>(builtIn);
    }
}

void TransactionExecutor::checkAndClear()
{
    std::unique_lock<std::shared_mutex> lock(m_stateStoragesMutex);

    if (m_stateStorages.empty())
    {
        return;
    }

    auto it = m_stateStorages.begin();

    if (it != m_lastUncommittedIterator)
    {
        if (m_cachedStorage)
        {
            m_cachedStorage->merge(true, *(it->storage));
            it = m_stateStorages.erase(it);
            if (it != m_stateStorages.end())
            {
                it->storage->setPrev(m_cachedStorage);
            }
        }
        else
        {
            it = m_stateStorages.erase(it);
            if (it != m_stateStorages.end())
            {
                it->storage->setPrev(m_backendStorage);
            }
        }
    }
}

std::unique_ptr<CallParameters> TransactionExecutor::createCallParameters(
    bcos::protocol::ExecutionMessage&& input, bool staticCall)
{
    auto callParameters = std::make_unique<CallParameters>(CallParameters::MESSAGE);
    callParameters->origin = input.origin();
    callParameters->senderAddress = input.from();
    callParameters->receiveAddress = input.to();
    callParameters->codeAddress = input.to();
    callParameters->create = input.create();
    callParameters->gas = input.gasAvailable();
    callParameters->data = input.takeData();
    callParameters->staticCall = staticCall;
    callParameters->create = input.create();
    callParameters->newEVMContractAddress = input.newEVMContractAddress();
    callParameters->status = 0;
    callParameters->keyLocks = input.takeKeyLocks();

    return callParameters;
}

std::unique_ptr<CallParameters> TransactionExecutor::createCallParameters(
    bcos::protocol::ExecutionMessage&& input, bcos::protocol::Transaction::Ptr&& tx)
{
    auto callParameters = std::make_unique<CallParameters>(CallParameters::MESSAGE);

    callParameters->origin.reserve(tx->sender().size() * 2);
    boost::algorithm::hex_lower(tx->sender(), std::back_inserter(callParameters->origin));

    callParameters->senderAddress = callParameters->origin;
    callParameters->receiveAddress = input.to();
    callParameters->codeAddress = input.to();

    callParameters->gas = input.gasAvailable();
    callParameters->data = tx->input().toBytes();  // TODO: add take data
    callParameters->staticCall = input.staticCall();
    callParameters->create = input.create();

    return callParameters;
}

std::shared_ptr<std::vector<std::string>> TransactionExecutor::getTxCriticals(
    const protocol::Transaction::ConstPtr& _tx)
{
    assert(_tx);

    if (_tx->type() == protocol::TransactionType::ContractCreation)
    {
        // Not to parallel contract creation transaction
        return nullptr;
    }

    // temp executive
    auto executive = createExecutive(m_blockContext, std::string(_tx->to()), 0, 0);
    auto p = executive->getPrecompiled(string(_tx->to()));
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
    auto parallelConfigPrecompiled =
        std::make_shared<precompiled::ParallelConfigPrecompiled>(m_hashImpl);

    EXECUTOR_LOG(TRACE) << LOG_DESC("[getTxCriticals] get parallel config")
                        << LOG_KV("receiveAddress", receiveAddress) << LOG_KV("selector", selector)
                        << LOG_KV("sender", _tx->sender());

    config = parallelConfigPrecompiled->getParallelConfig(
        executive, receiveAddress, selector, _tx->sender());

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
}