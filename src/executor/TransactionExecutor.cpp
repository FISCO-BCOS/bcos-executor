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
// #include "../precompiled/CNSPrecompiled.h"
// #include "../precompiled/CRUDPrecompiled.h"
// #include "../precompiled/ConsensusPrecompiled.h"
// #include "../precompiled/CryptoPrecompiled.h"
// #include "../precompiled/DeployWasmPrecompiled.h"
// #include "../precompiled/FileSystemPrecompiled.h"
// #include "../precompiled/KVTableFactoryPrecompiled.h"
// #include "../precompiled/ParallelConfigPrecompiled.h"
// #include "../precompiled/PrecompiledResult.h"
// #include "../precompiled/SystemConfigPrecompiled.h"
// #include "../precompiled/TableFactoryPrecompiled.h"
// #include "../precompiled/Utilities.h"
// #include "../precompiled/extension/DagTransferPrecompiled.h"
#include "../ChecksumAddress.h"
#include "../vm/BlockContext.h"
#include "../vm/Common.h"
#include "../vm/Precompiled.h"
#include "../vm/TransactionExecutive.h"
#include "Common.h"
#include "TxDAG.h"
#include "bcos-framework/interfaces/dispatcher/SchedulerInterface.h"
#include "bcos-framework/interfaces/executor/PrecompiledTypeDef.h"
#include "bcos-framework/interfaces/protocol/TransactionReceipt.h"
#include "bcos-framework/interfaces/storage/Table.h"
#include "bcos-framework/libcodec/abi/ContractABIType.h"
#include "bcos-framework/libstorage/StateStorage.h"
#include "bcos-framework/libutilities/Error.h"
#include "bcos-framework/libutilities/ThreadPool.h"
#include "interfaces/executor/ExecutionParams.h"
#include "interfaces/executor/ExecutionResult.h"
#include "interfaces/storage/StorageInterface.h"
#include "libprotocol/LogEntry.h"
#include <tbb/parallel_for.h>
#include <boost/algorithm/hex.hpp>
#include <boost/exception/detail/exception_ptr.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/throw_exception.hpp>
#include <exception>
#include <iterator>
#include <string>
#include <thread>

using namespace bcos;
using namespace std;
using namespace bcos::executor;
using namespace bcos::protocol;
using namespace bcos::storage;
using namespace bcos::precompiled;

TransactionExecutor::TransactionExecutor(txpool::TxPoolInterface::Ptr txpool,
    storage::TransactionalStorageInterface::Ptr backendStorage,
    protocol::ExecutionResultFactory::Ptr executionResultFactory, bcos::crypto::Hash::Ptr hashImpl,
    bool isWasm, size_t poolSize)
  : m_txpool(std::move(txpool)),
    m_backendStorage(std::move(backendStorage)),
    m_executionResultFactory(std::move(executionResultFactory)),
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

    m_lastUncommitedIterator = m_stateStorages.begin();

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

    m_threadPool = std::make_shared<ThreadPool>("asyncTasks", poolSize);
}

void TransactionExecutor::nextBlockHeader(const protocol::BlockHeader::ConstPtr& blockHeader,
    std::function<void(bcos::Error::Ptr&&)> callback) noexcept
{
    try
    {
        EXECUTOR_LOG(INFO) << "NextBlockHeader request: "
                           << LOG_KV("number", blockHeader->number());

        bcos::storage::StateStorage::Ptr stateStorage;
        if (m_stateStorages.empty())
        {
            stateStorage = std::make_shared<bcos::storage::StateStorage>(
                m_backendStorage, m_hashImpl, blockHeader->number());
        }
        else
        {
            auto prev = m_stateStorages.back();
            stateStorage = std::make_shared<bcos::storage::StateStorage>(
                std::move(prev), m_hashImpl, blockHeader->number());
        }

        m_blockContext = std::make_shared<BlockContext>(stateStorage, m_hashImpl, blockHeader,
            m_executionResultFactory, EVMSchedule(), m_isWasm);

        m_blockContext->setPrecompiledContract(m_precompiledContract);
        m_stateStorages.push_back(std::move(stateStorage));

        if (m_lastUncommitedIterator == m_stateStorages.end())
        {
            m_lastUncommitedIterator = m_stateStorages.cend();
            --m_lastUncommitedIterator;
        }

        EXECUTOR_LOG(INFO) << "NextBlockHeader success";
        callback(nullptr);
    }
    catch (std::exception& e)
    {
        EXECUTOR_LOG(ERROR) << "NextBlockHeader error: " << boost::diagnostic_information(e);

        callback(BCOS_ERROR_WITH_PREV_PTR(-1, "nextBlockHeader unknown error", e));
    }
}

void TransactionExecutor::dagExecuteTransactions(
    const gsl::span<bcos::protocol::ExecutionParams::ConstPtr>& inputs,
    std::function<void(bcos::Error::Ptr&&, std::vector<bcos::protocol::ExecutionResult::Ptr>&&)>
        callback) noexcept
{
    // TODO: try to execute use DAG
    (void)inputs;
    (void)callback;
}

void TransactionExecutor::call(const bcos::protocol::ExecutionParams::ConstPtr& input,
    std::function<void(bcos::Error::Ptr&&, bcos::protocol::ExecutionResult::Ptr&&)>
        callback) noexcept
{
    EXECUTOR_LOG(INFO) << "Call request";
    asyncExecute(input, true,
        [callback = std::move(callback)](
            Error::Ptr&& error, bcos::protocol::ExecutionResult::Ptr&& result) {
            if (error)
            {
                std::string errorMessage = "Call failed: " + boost::diagnostic_information(*error);
                EXECUTOR_LOG(ERROR) << errorMessage;
                callback(BCOS_ERROR_WITH_PREV_PTR(-1, errorMessage, *error), nullptr);
                return;
            }

            EXECUTOR_LOG(INFO) << "Call success";
            callback(std::move(error), std::move(result));
        });
}

void TransactionExecutor::executeTransaction(const protocol::ExecutionParams::ConstPtr& input,
    std::function<void(Error::Ptr&&, protocol::ExecutionResult::Ptr&&)> callback) noexcept
{
    EXECUTOR_LOG(INFO) << "ExecuteTransaction request";
    asyncExecute(input, false,
        [callback = std::move(callback)](
            Error::Ptr&& error, bcos::protocol::ExecutionResult::Ptr&& result) {
            if (error)
            {
                std::string errorMessage =
                    "ExecuteTransaction failed: " + boost::diagnostic_information(*error);
                EXECUTOR_LOG(ERROR) << errorMessage;
                callback(BCOS_ERROR_WITH_PREV_PTR(-1, errorMessage, *error), nullptr);
                return;
            }

            EXECUTOR_LOG(INFO) << "ExecuteTransaction success";
            callback(std::move(error), std::move(result));
        });
}

void TransactionExecutor::getTableHashes(bcos::protocol::BlockNumber number,
    std::function<void(
        bcos::Error::Ptr&&, std::vector<std::tuple<std::string, crypto::HashType>>&&)>
        callback) noexcept
{
    EXECUTOR_LOG(INFO) << "GetTableHashes" << LOG_KV("number", number);
    if (m_stateStorages.empty())
    {
        EXECUTOR_LOG(ERROR) << "GetTableHashes error: No uncommited state in executor";
        callback(BCOS_ERROR_PTR(-1, "No uncommited state in executor"),
            std::vector<std::tuple<std::string, crypto::HashType>>());
        return;
    }

    auto last = m_stateStorages.front();
    if (last->blockNumber() != number)
    {
        auto errorMessage = "GetTableHashes error: Request block number: " +
                            boost::lexical_cast<std::string>(number) +
                            " not equal to last blockNumber: " +
                            boost::lexical_cast<std::string>(last->blockNumber());

        EXECUTOR_LOG(ERROR) << errorMessage;
        callback(BCOS_ERROR_PTR(-1, errorMessage),
            std::vector<std::tuple<std::string, crypto::HashType>>());

        return;
    }

    auto tableHashes = last->tableHashes();
    EXECUTOR_LOG(INFO) << "GetTableHashes success" << LOG_KV("size", tableHashes.size());

    callback(nullptr, std::move(tableHashes));
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

    auto last = m_lastUncommitedIterator;
    if (last == m_stateStorages.end())
    {
        auto errorMessage = "Prepare error: empty stateStorages";
        EXECUTOR_LOG(ERROR) << errorMessage;
        callback(BCOS_ERROR_PTR(-1, errorMessage));

        return;
    }

    if ((*last)->blockNumber() != params.number)
    {
        auto errorMessage = "Prepare error: Request block number: " +
                            boost::lexical_cast<std::string>(params.number) +
                            " not equal to last blockNumber: " +
                            boost::lexical_cast<std::string>((*last)->blockNumber());

        EXECUTOR_LOG(ERROR) << errorMessage;
        callback(BCOS_ERROR_PTR(-1, errorMessage));

        return;
    }

    bcos::storage::TransactionalStorageInterface::TwoPCParams storageParams;
    storageParams.number = params.number;
    m_backendStorage->asyncPrepare(
        storageParams, *last, [callback = std::move(callback)](auto&& error) {
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

    if (m_lastUncommitedIterator == m_stateStorages.end())
    {
        EXECUTOR_LOG(ERROR) << "Commit error: No uncommited state in executor";
        callback(BCOS_ERROR_PTR(-1, "No uncommited state in executor"));
        return;
    }

    auto last = *m_lastUncommitedIterator;
    if (last->blockNumber() != params.number)
    {
        auto errorMessage = "Commit error: Request block number: " +
                            boost::lexical_cast<std::string>(params.number) +
                            " not equal to last blockNumber: " +
                            boost::lexical_cast<std::string>(last->blockNumber());

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

            ++m_lastUncommitedIterator;
            m_blockContext = nullptr;

            callback(nullptr);
        });
}

void TransactionExecutor::rollback(
    const TwoPCParams& params, std::function<void(bcos::Error::Ptr&&)> callback) noexcept
{
    EXECUTOR_LOG(INFO) << "Rollback request: " << LOG_KV("number", params.number);

    if (m_lastUncommitedIterator == m_stateStorages.end())
    {
        EXECUTOR_LOG(ERROR) << "Rollback error: No uncommited state in executor";
        callback(BCOS_ERROR_PTR(-1, "No uncommited state in executor"));
        return;
    }

    auto last = *m_lastUncommitedIterator;
    if (last->blockNumber() != params.number)
    {
        auto errorMessage = "Rollback error: Request block number: " +
                            boost::lexical_cast<std::string>(params.number) +
                            " not equal to last blockNumber: " +
                            boost::lexical_cast<std::string>(last->blockNumber());

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

void TransactionExecutor::asyncExecute(const bcos::protocol::ExecutionParams::ConstPtr& input,
    bool staticCall,
    std::function<void(bcos::Error::Ptr&&, bcos::protocol::ExecutionResult::Ptr&&)> callback)
{
    std::shared_ptr<BlockContext> blockContext;

    if (staticCall)
    {
        // create a temp blockContext
    }
    else
    {
        if (!m_blockContext)
        {
            callback(BCOS_ERROR_PTR(-1, "Execute failed with empty blockContext!"), nullptr);
            return;
        }

        blockContext = m_blockContext;
    }

    std::string contract;
    bool create = false;
    if (input->to().empty() && !m_isWasm)
    {
        create = true;
        if (input->createSalt())
        {
            contract = newEVMAddress(input->from(), input->input(), input->createSalt().value());
        }
        else
        {
            contract =
                newEVMAddress(input->from(), blockContext->currentNumber(), input->contextID());
        }
    }
    else
    {
        contract = input->to();
    }

    auto callParameters = std::make_shared<CallParameters>();
    callParameters->type = CallParameters::MESSAGE;
    callParameters->origin = input->origin();
    callParameters->senderAddress = input->from();
    callParameters->receiveAddress = contract;
    callParameters->codeAddress = contract;
    callParameters->create = create;
    callParameters->gas = input->gasAvailable();
    callParameters->data = input->input().toBytes();
    callParameters->staticCall = staticCall;

    switch (input->type())
    {
    case bcos::protocol::ExecutionParams::TXHASH:
    {
        // Get transaction first
        auto txhashes = std::make_shared<bcos::crypto::HashList>();
        txhashes->push_back(input->transactionHash());

        m_txpool->asyncFillBlock(std::move(txhashes),
            [this, input, hash = input->transactionHash(), blockContext = std::move(blockContext),
                callParameters = std::move(callParameters), contract = std::move(contract),
                callback](Error::Ptr error, bcos::protocol::TransactionsPtr transactons) {
                if (error)
                {
                    callback(BCOS_ERROR_WITH_PREV_PTR(
                                 -1, "Transaction does not exists: " + hash.hex(), *error),
                        nullptr);
                    return;
                }

                if (transactons->empty())
                {
                    callback(
                        BCOS_ERROR_PTR(-1, "Transaction does not exists: " + hash.hex()), nullptr);
                    return;
                }

                auto tx = (*transactons)[0];
                auto executive = std::make_shared<TransactionExecutive>(blockContext, contract,
                    input->contextID(),
                    std::bind(&TransactionExecutor::onCallResultsCallback, this,
                        std::placeholders::_1, std::placeholders::_2));

                blockContext->insertExecutive(input->contextID(), contract, {executive, callback});

                callParameters->data = tx->input().toBytes();
                auto sender = tx->sender();
                boost::algorithm::hex_lower(sender.begin(), sender.end(),
                    std::back_inserter(callParameters->senderAddress));
                // callParameters->senderAddress = boost::algorithm::hex_lower(tx->sender());
                callParameters->origin = tx->sender();

                try
                {
                    executive->start(std::move(callParameters));
                }
                catch (std::exception& e)
                {
                    EXECUTOR_LOG(ERROR) << "Execute error: " << boost::diagnostic_information(e);
                    callback(BCOS_ERROR_WITH_PREV_PTR(-1, "Execute error", e), nullptr);
                }
            });

        break;
    }
    case bcos::protocol::ExecutionParams::EXTERNAL_CALL:
    {
        auto executive =
            std::make_shared<TransactionExecutive>(blockContext, contract, input->contextID(),
                std::bind(&TransactionExecutor::onCallResultsCallback, this, std::placeholders::_1,
                    std::placeholders::_2));

        blockContext->insertExecutive(input->contextID(), contract, {executive, callback});

        try
        {
            executive->start(std::move(callParameters));
        }
        catch (std::exception& e)
        {
            EXECUTOR_LOG(ERROR) << "Execute error: " << boost::diagnostic_information(e);
            callback(BCOS_ERROR_WITH_PREV_PTR(-1, "Execute error", e), nullptr);
        }

        break;
    }
    case bcos::protocol::ExecutionParams::EXTERNAL_RETURN:
    {
        auto executive = blockContext->getExecutive(input->contextID(), input->to());
        std::get<1>(executive) = callback;

        // call the sink

        // TODO: convert ExecutionParameters to CallParameters
        CallParameters::Ptr callParameters;

        std::get<0>(executive)->pushMessage(std::move(callParameters));

        break;
    }
    default:
    {
        EXECUTOR_LOG(ERROR) << "Unknown type: " << input->type();
        callback(
            BCOS_ERROR_PTR(-1, "Unknown type" + boost::lexical_cast<std::string>(input->type())),
            nullptr);
        return;
    }
    }
}

void TransactionExecutor::onCallResultsCallback(
    TransactionExecutive::Ptr executive, std::shared_ptr<CallParameters>&& response)
{
    auto it = m_blockContext->getExecutive(executive->contextID(), executive->contractAddress());
    auto executionResult = m_executionResultFactory->createExecutionResult();
    executionResult->setMessage(std::move(response->message));
    if (response->type == CallParameters::MESSAGE)
    {
        executionResult->setType(ExecutionResult::EXTERNAL_CALL);
    }
    else
    {
        executionResult->setType(ExecutionResult::FINISHED);
    }
    executionResult->setTo(response->receiveAddress);
    executionResult->setStatus(response->status);
    executionResult->setContextID(executive->contextID());
    if (response->createSalt)
    {
        executionResult->setCreateSalt(*response->createSalt);
    }
    executionResult->setStaticCall(response->staticCall);
    executionResult->setNewEVMContractAddress(response->newEVMContractAddress);
    executionResult->setOutput(std::move(response->data));
    executionResult->setLogEntries(std::make_shared<LogEntries>(std::move(response->logEntries)));

    std::get<1>(it)(nullptr, std::move(executionResult));
}

BlockContext::Ptr TransactionExecutor::createBlockContext(
    const protocol::BlockHeader::ConstPtr& currentHeader, storage::StateStorage::Ptr tableFactory)
{
    (void)m_version;  // TODO: accord to m_version to chose schedule
    BlockContext::Ptr context = make_shared<BlockContext>(tableFactory, m_hashImpl, currentHeader,
        m_executionResultFactory, FiscoBcosScheduleV3, m_isWasm);

    // TODO: System contract need to redesign
    // auto tableFactoryPrecompiled =
    //     std::make_shared<precompiled::TableFactoryPrecompiled>(m_hashImpl);
    // tableFactoryPrecompiled->setMemoryTableFactory(tableFactory);
    // auto sysConfig = std::make_shared<precompiled::SystemConfigPrecompiled>(m_hashImpl);
    // auto parallelConfigPrecompiled =
    //     std::make_shared<precompiled::ParallelConfigPrecompiled>(m_hashImpl);
    // auto consensusPrecompiled = std::make_shared<precompiled::ConsensusPrecompiled>(m_hashImpl);
    // auto cnsPrecompiled = std::make_shared<precompiled::CNSPrecompiled>(m_hashImpl);

    // context->setAddress2Precompiled(SYS_CONFIG_ADDRESS, sysConfig);
    // context->setAddress2Precompiled(TABLE_FACTORY_ADDRESS, tableFactoryPrecompiled);
    // context->setAddress2Precompiled(CONSENSUS_ADDRESS, consensusPrecompiled);
    // context->setAddress2Precompiled(CNS_ADDRESS, cnsPrecompiled);
    // context->setAddress2Precompiled(PARALLEL_CONFIG_ADDRESS, parallelConfigPrecompiled);
    // auto kvTableFactoryPrecompiled =
    //     std::make_shared<precompiled::KVTableFactoryPrecompiled>(m_hashImpl);
    // kvTableFactoryPrecompiled->setMemoryTableFactory(tableFactory);
    // context->setAddress2Precompiled(KV_TABLE_FACTORY_ADDRESS, kvTableFactoryPrecompiled);
    // context->setAddress2Precompiled(
    //     CRYPTO_ADDRESS, std::make_shared<precompiled::CryptoPrecompiled>(m_hashImpl));
    // context->setAddress2Precompiled(
    //     DAG_TRANSFER_ADDRESS, std::make_shared<precompiled::DagTransferPrecompiled>(m_hashImpl));
    // context->setAddress2Precompiled(
    //     CRYPTO_ADDRESS, std::make_shared<CryptoPrecompiled>(m_hashImpl));
    // context->setAddress2Precompiled(
    //     DEPLOY_WASM_ADDRESS, std::make_shared<DeployWasmPrecompiled>(m_hashImpl));
    // context->setAddress2Precompiled(
    //     CRUD_ADDRESS, std::make_shared<precompiled::CRUDPrecompiled>(m_hashImpl));
    // context->setAddress2Precompiled(
    //     BFS_ADDRESS, std::make_shared<precompiled::FileSystemPrecompiled>(m_hashImpl));

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

    // getTxGasLimitToContext from precompiled and set to context
    // auto ret = sysConfig->getSysConfigByKey(ledger::SYSTEM_KEY_TX_GAS_LIMIT, tableFactory);
    // context->setTxGasLimit(boost::lexical_cast<uint64_t>(ret.first));
    // context->setTxCriticalsHandler([&](const protocol::Transaction::ConstPtr& _tx)
    //                                    -> std::shared_ptr<std::vector<std::string>> {
    //     if (_tx->type() == protocol::TransactionType::ContractCreation)
    //     {
    //         // Not to parallel contract creation transaction
    //         return nullptr;
    //     }

    //     auto p = context->getPrecompiled(string(_tx->to()));
    //     if (p)
    //     {
    //         // Precompile transaction
    //         if (p->isParallelPrecompiled())
    //         {
    //             auto ret = make_shared<vector<string>>(p->getParallelTag(_tx->input()));
    //             for (string& critical : *ret)
    //             {
    //                 critical += _tx->to();
    //             }
    //             return ret;
    //         }
    //         else
    //         {
    //             return nullptr;
    //         }
    //     }
    //     else
    //     {
    //         uint32_t selector = precompiled::getParamFunc(_tx->input());

    //         auto receiveAddress = _tx->to();
    //         std::shared_ptr<precompiled::ParallelConfig> config = nullptr;
    //         // hit the cache, fetch ParallelConfig from the cache directly
    //         // Note: Only when initializing DAG, get ParallelConfig, will not get
    //         ParallelConfig
    //         // during transaction execution
    //         auto parallelKey = std::make_pair(string(receiveAddress), selector);
    //         if (context->getParallelConfigCache()->count(parallelKey))
    //         {
    //             config = context->getParallelConfigCache()->at(parallelKey);
    //         }
    //         else
    //         {
    //             config = parallelConfigPrecompiled->getParallelConfig(
    //                 context, receiveAddress, selector, _tx->sender());
    //             context->getParallelConfigCache()->insert(std::make_pair(parallelKey,
    //             config));
    //         }

    //         if (config == nullptr)
    //         {
    //             return nullptr;
    //         }
    //         else
    //         {
    //             // Testing code
    //             auto res = make_shared<vector<string>>();

    //             codec::abi::ABIFunc af;
    //             bool isOk = af.parser(config->functionName);
    //             if (!isOk)
    //             {
    //                 EXECUTOR_LOG(DEBUG)
    //                     << LOG_DESC("[getTxCriticals] parser function signature failed, ")
    //                     << LOG_KV("func signature", config->functionName);

    //                 return nullptr;
    //             }

    //             auto paramTypes = af.getParamsType();
    //             if (paramTypes.size() < (size_t)config->criticalSize)
    //             {
    //                 EXECUTOR_LOG(DEBUG)
    //                     << LOG_DESC("[getTxCriticals] params type less than  criticalSize")
    //                     << LOG_KV("func signature", config->functionName)
    //                     << LOG_KV("func criticalSize", config->criticalSize);

    //                 return nullptr;
    //             }

    //             paramTypes.resize((size_t)config->criticalSize);

    //             codec::abi::ContractABICodec abi(m_hashImpl);
    //             isOk = abi.abiOutByFuncSelector(_tx->input().getCroppedData(4), paramTypes,
    //             *res); if (!isOk)
    //             {
    //                 EXECUTOR_LOG(DEBUG) << LOG_DESC("[getTxCriticals] abiout failed, ")
    //                                     << LOG_KV("func signature", config->functionName);

    //                 return nullptr;
    //             }

    //             for (string& critical : *res)
    //             {
    //                 critical += _tx->to();
    //             }

    //             return res;
    //         }
    //     }
    // });

    return context;
}

std::string TransactionExecutor::newEVMAddress(
    const std::string_view& sender, int64_t blockNumber, int64_t contextID)
{
    auto hash =
        m_hashImpl->hash(std::string(sender) + boost::lexical_cast<std::string>(blockNumber) +
                         boost::lexical_cast<std::string>(contextID));

    std::string hexAddress;
    boost::algorithm::hex(hash.data(), hash.data() + 20, std::back_inserter(hexAddress));

    toChecksumAddress(hexAddress, m_hashImpl);

    return hexAddress;
}

std::string TransactionExecutor::newEVMAddress(
    const std::string_view& _sender, bytesConstRef _init, u256 const& _salt)
{
    auto hash = m_hashImpl->hash(
        bytes{0xff} + toBytes(_sender) + toBigEndian(_salt) + m_hashImpl->hash(_init));
    return string((char*)hash.data(), 20);
}

// protocol::ExecutionResult::Ptr TransactionExecutor::createExecutionResult(
//     std::shared_ptr<TransactionExecutive> executive)
// {
// auto executionResult = m_executionResultFactory->createExecutionResult();
// executionResult->setContextID(executive->contextID());
// executionResult->setStatus((int32_t)executive->status());
// executionResult->setGasAvailable(executive->gasLeft());
// executionResult->setLogEntries(executive->logs());
// executionResult->setOutput(std::move(executive->takeOutput().takeBytes()));
// executionResult->setStaticCall(executive->callParameters().staticCall);
// executionResult->setStaticCall(bool staticCall)
// }