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
#include "../precompiled/CNSPrecompiled.h"
#include "../precompiled/CRUDPrecompiled.h"
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
#include "../state/State.h"
#include "../vm/BlockContext.h"
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
#include "bcos-framework/libutilities/ThreadPool.h"
#include <tbb/parallel_for.h>
#include <exception>
#include <thread>

using namespace bcos;
using namespace std;
using namespace bcos::executor;
using namespace bcos::protocol;
using namespace bcos::storage;
using namespace bcos::precompiled;

TransactionExecutor::TransactionExecutor(const protocol::BlockFactory::Ptr& _blockFactory,
    const std::shared_ptr<dispatcher::SchedulerInterface>& _scheduler,
    const ledger::LedgerInterface::Ptr& _ledger, const txpool::TxPoolInterface::Ptr& _txpool,
    const std::shared_ptr<storage::TransactionalStorageInterface>& _backendStorage,
    const std::shared_ptr<storage::MergeableStorageInterface>& _cacheStorage,
    protocol::ExecutionResultFactory::Ptr _executionResultFactory, bool _isWasm, size_t _poolSize)
  : m_blockFactory(_blockFactory),
    m_scheduler(_scheduler),
    m_ledger(_ledger),
    m_txpool(_txpool),
    m_backendStorage(_backendStorage),
    m_cacheStorage(_cacheStorage),
    m_executionResultFactory(_executionResultFactory),
    m_isWasm(_isWasm),
    m_version(Version_3_0_0)  // current executor version, will set as new block's version
{
    assert(m_blockFactory);
    assert(m_scheduler);
    assert(m_ledger);
    assert(m_backendStorage);
    m_threadNum = std::max(std::thread::hardware_concurrency(), (unsigned int)1);
    m_hashImpl = m_blockFactory->cryptoSuite()->hashImpl();
    auto fillZero = [](int _num) -> std::string {
        std::stringstream stream;
        stream << std::setfill('0') << std::setw(40) << std::hex << _num;
        return stream.str();
    };
    m_precompiledContract.insert(std::make_pair(fillZero(1),
        make_shared<PrecompiledContract>(3000, 0, PrecompiledRegistrar::executor("ecrecover"))));
    m_precompiledContract.insert(std::make_pair(fillZero(2),
        make_shared<PrecompiledContract>(60, 12, PrecompiledRegistrar::executor("sha256"))));
    m_precompiledContract.insert(std::make_pair(fillZero(3),
        make_shared<PrecompiledContract>(600, 120, PrecompiledRegistrar::executor("ripemd160"))));
    m_precompiledContract.insert(std::make_pair(fillZero(4),
        make_shared<PrecompiledContract>(15, 3, PrecompiledRegistrar::executor("identity"))));
    m_precompiledContract.insert(
        {fillZero(5), make_shared<PrecompiledContract>(PrecompiledRegistrar::pricer("modexp"),
                          PrecompiledRegistrar::executor("modexp"))});
    m_precompiledContract.insert(
        {fillZero(6), make_shared<PrecompiledContract>(
                          150, 0, PrecompiledRegistrar::executor("alt_bn128_G1_add"))});
    m_precompiledContract.insert(
        {fillZero(7), make_shared<PrecompiledContract>(
                          6000, 0, PrecompiledRegistrar::executor("alt_bn128_G1_mul"))});
    m_precompiledContract.insert({fillZero(8),
        make_shared<PrecompiledContract>(PrecompiledRegistrar::pricer("alt_bn128_pairing_product"),
            PrecompiledRegistrar::executor("alt_bn128_pairing_product"))});
    m_precompiledContract.insert({fillZero(9),
        make_shared<PrecompiledContract>(PrecompiledRegistrar::pricer("blake2_compression"),
            PrecompiledRegistrar::executor("blake2_compression"))});

    // CallBackFunction convert ledger asyncGetBlockHashByNumber to sync
    m_pNumberHash = [this](protocol::BlockNumber _number) -> crypto::HashType {
        std::promise<crypto::HashType> prom;
        auto returnHandler = [&prom](Error::Ptr error, crypto::HashType const& hash) {
            if (error)
            {
                EXECUTOR_LOG(WARNING)
                    << LOG_BADGE("executor") << LOG_DESC("asyncGetBlockHashByNumber failed")
                    << LOG_KV("message", error->errorMessage());
            }
            prom.set_value(hash);
        };
        m_ledger->asyncGetBlockHashByNumber(_number, returnHandler);
        auto fut = prom.get_future();
        return fut.get();
    };
    m_threadPool = std::make_shared<ThreadPool>("asyncTasks", _poolSize);
    // use ledger to get lastest header
    m_lastHeader = getLatestHeaderFromStorage();
    m_tableStorage =
        std::make_shared<StateStorage>(m_backendStorage, m_hashImpl, m_lastHeader->number() + 1);
}

void TransactionExecutor::nextBlockHeader(const protocol::BlockHeader::ConstPtr& blockHeader,
    std::function<void(bcos::Error::Ptr&&)> callback) noexcept
{
    m_lastHeader = blockHeader;
    auto storageBlockNumber = getLatestBlockNumberFromStorage();
    // use the constructor of StateStorage to import the data of m_tableStorage
    m_tableStorage = std::make_shared<StateStorage>(
        m_tableStorage, m_backendStorage, m_hashImpl, m_lastHeader->number(), storageBlockNumber);
    m_blockContext->clear();
    m_blockContext = createBlockContext(m_lastHeader, m_tableStorage);
    callback(nullptr);
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
    // FIXME: multi thread call, find somewhere to hold blockContext, add a map<int64_t,
    // blockContext::Ptr> m_executivesOfCall
    BlockContext::Ptr blockContext = nullptr;
    if (m_contextsOfCall.count(input->contextID()))
    {
        blockContext = m_contextsOfCall[input->contextID()];
    }
    else
    {
        auto storageHeader = getLatestHeaderFromStorage();
        auto tableStorage =
            std::make_shared<StateStorage>(m_backendStorage, m_hashImpl, storageHeader->number());
        auto currentHeader =
            m_blockFactory->blockHeaderFactory()->populateBlockHeader(storageHeader);
        blockContext = createBlockContext(currentHeader, tableStorage);
        m_contextsOfCall[input->contextID()] = blockContext;
    }
    if (input->type() == ExecutionParams::TXHASH || input->type() == ExecutionParams::EXTERNAL_CALL)
    {
        auto executive = std::make_shared<TransactionExecutive>(blockContext, input->contextID());
        blockContext->insertExecutive(input->contextID(), input->to(), executive);
        // create a new tx use input
        auto tx = createTransaction(input);
        executive->setReturnCallback(callback);
        // TODO: modify callback to remove blockContext from map if execution finished
        // after the thread in executive finished, the use_count of executive will decrease to zero
        // after the executive release the blockContext will release
        asyncExecute(tx, executive, false);
    }
    else if (input->type() == ExecutionParams::EXTERNAL_RETURN)
    {  // scheduler promises that the contextID only appear once every execution
        // use to() and contextID to find TransactionExecutive
        auto executive = m_blockContext->getLastExecutiveOf(input->contextID(), input->to());
        // set new return callback to executive and continue execution
        executive->setReturnCallback(callback);
        executive->continueExecution(
            input->input().toBytes(), input->status(), input->gasAvailable(), input->from());
    }
    else
    {  // unknown type warning and callback to scheduler
        EXECUTOR_LOG(ERROR) << LOG_BADGE("executeTransaction unknown type")
                            << LOG_KV("contextID", input->contextID());
        // TODO: use right errorCode
        callback(make_shared<Error>(-1, "unknown type"), nullptr);
    }
}

protocol::Transaction::Ptr TransactionExecutor::createTransaction(
    const bcos::protocol::ExecutionParams::ConstPtr& input)
{
    // create transaction use transactionFactory
    auto transactionFactory = m_blockFactory->transactionFactory();
    auto transaction = transactionFactory->createTransaction(
        0, input->to(), input->input().toBytes(), u256(0), 0, "", "", 0);
    return transaction;
}

void TransactionExecutor::executeTransaction(const protocol::ExecutionParams::ConstPtr& input,
    std::function<void(Error::Ptr&&, protocol::ExecutionResult::Ptr&&)> callback) noexcept
{
    // TODO: if finished call the return callback with finished message, exit the thread and pop
    // TransactionExecutive id stack is empty remove the address in map
    // TODO: if EXTERNAL_CALL, set the continue callback with promise and call the return
    // callback, when future got return continue to run

    auto createExecutive = [&](unsigned depth) {
        auto executive =
            std::make_shared<TransactionExecutive>(m_blockContext, input->contextID(), depth);
        auto caller = string(input->to());
        if (input->to().empty())
        {  // FIXME: update framework and uncomment code below
#if 0
            if (input->createSalt())
            {  // input->createSalt() is not empty use create2
                caller = executive->newEVMAddress(
                    input->from(), input->input(), input->createSalt().value());
            }
            else
            {
                caller = executive->newEVMAddress(input->from());
            }
#endif
        }
        m_blockContext->insertExecutive(input->contextID(), caller, executive);
        return executive;
    };
    if (input->type() == ExecutionParams::TXHASH)
    {  // type is TXHASH, pull the transaction use txpool and create a TransactionExecutive
        auto executive = createExecutive(0);
        // fetch transaction use m_txpool
        auto txHash = make_shared<std::vector<crypto::HashType>>();
        txHash->push_back(input->transactionHash());

        promise<protocol::Transaction::Ptr> prom;
        m_txpool->asyncFillBlock(
            txHash, [&prom](Error::Ptr error, bcos::protocol::TransactionsPtr transactions) {
                if (error)
                {
                    prom.set_value(nullptr);
                }
                else
                {
                    prom.set_value(transactions->front());
                }
            });
        auto tx = prom.get_future().get();
        // set return callback to TransactionExecutive
        executive->setReturnCallback(callback);
        asyncExecute(tx, executive, false);
    }
    else if (input->type() == ExecutionParams::EXTERNAL_CALL)
    {  // type is EXTERNAL_CALL create TransactionExecutive and tx to execute
        auto executive = createExecutive(input->depth());
        executive->setReturnCallback(callback);
        // create a new tx use ExecutionParams
        auto tx = createTransaction(input);
        // FIXME: use input->staticCall() replace false
        asyncExecute(tx, executive, false);
    }
    else if (input->type() == ExecutionParams::EXTERNAL_RETURN)
    {  // scheduler promises that the contextID only appear once every execution
        // use to() and contextID to find TransactionExecutive
        auto executive = m_blockContext->getLastExecutiveOf(input->contextID(), input->to());
        // set new return callback to executive and continue execution
        executive->setReturnCallback(callback);
        executive->continueExecution(
            input->input().toBytes(), input->status(), input->gasAvailable(), input->from());
    }
    else
    {  // unknown type warning and callback to scheduler
        EXECUTOR_LOG(ERROR) << LOG_BADGE("executeTransaction unknown type")
                            << LOG_KV("contextID", input->contextID());
        // TODO: use right errorCode
        callback(make_shared<Error>(-1, "unknown type"), nullptr);
    }
}

void TransactionExecutor::getTableHashes(bcos::protocol::BlockNumber number,
    std::function<void(
        bcos::Error::Ptr&&, std::vector<std::tuple<std::string, crypto::HashType>>&&)>
        callback) noexcept
{
    if (m_tableStorage->blockNumber() != number)
    {
        EXECUTOR_LOG(ERROR) << LOG_BADGE("getTableHashes mismatch number")
                            << LOG_KV("number", number);
        // TODO: use right errorCode
        callback(make_shared<Error>(-1, "mismatch number"),
            std::vector<std::tuple<std::string, crypto::HashType>>());
    }
    // TODO: calculate hash of tables, use threadpool to calculate hash
    callback(nullptr, m_tableStorage->tableHashes());
}

void TransactionExecutor::prepare(
    const TwoPCParams& params, std::function<void(bcos::Error::Ptr&&)> callback) noexcept
{
    storage::StateStorage::Ptr tableStorage = nullptr;
    {  // add a lock of m_tableStorage
        lock_guard l(m_tableStorageMutex);
        if (m_tableStorage->blockNumber() == params.number)
        {
            tableStorage = m_tableStorage;
        }
    }
    if (tableStorage == nullptr)
    {  // get tableStorage from m_uncommittedData
        lock_guard l(m_uncommittedDataMutex);
        tableStorage = m_uncommittedData.front();
        if (tableStorage->blockNumber() != params.number)
        {
            EXECUTOR_LOG(FATAL) << "check blockNumber continuity of prepare failed"
                                << LOG_KV("top", tableStorage->blockNumber())
                                << LOG_KV("commit", params.number);
        }
        m_uncommittedData.pop();
    }
    // call m_backendStorage prepare
    m_backendStorage->asyncPrepare(TransactionalStorageInterface::TwoPCParams{params.number},
        tableStorage,
        [callback](Error::Ptr&& error) { callback(std::forward<Error::Ptr>(error)); });
}

void TransactionExecutor::commit(
    const TwoPCParams& params, std::function<void(bcos::Error::Ptr&&)> callback) noexcept
{
    // add the m_tableStorage of number to m_cacheStorage
    // FIXME: get tableStorage from m_uncommittedData or m_tableStorage
    (void)params;
    m_cacheStorage->merge(m_tableStorage);
    callback(nullptr);
}

void TransactionExecutor::rollback(
    const TwoPCParams& params, std::function<void(bcos::Error::Ptr&&)> callback) noexcept
{  // FIXME: do nothing for now
    (void)params;
    (void)callback;
}

void TransactionExecutor::asyncExecute(protocol::Transaction::ConstPtr transaction,
    TransactionExecutive::Ptr executive, bool staticCall = false)
{  // TransactionExecutive use a new thread to execute contract
    executive->setWorker(make_shared<thread>(
        [transaction, executive, executionResultFactory = m_executionResultFactory, staticCall]() {
            executive->reset();
            try
            {  // OK - transaction looks valid - execute.
                executive->initialize(transaction);
                if (!executive->execute(staticCall))
                    executive->go();
                executive->finalize();
            }
            catch (Exception const& _e)
            {  // only OutOfGasBase ExecutorNotFound exception will throw
                EXECUTOR_LOG(ERROR) << "executeTransaction Exception" << diagnostic_information(_e);
            }
            catch (std::exception const& _e)
            {
                EXECUTOR_LOG(ERROR)
                    << "executeTransaction std::exception" << boost::diagnostic_information(_e);
            }

            executive->loggingException();
            auto result = executionResultFactory->createExecutionResult();
            result->setContextID(executive->getContextID());
            result->setStatus((int32_t)executive->status());
            // FIXME: set error message
            // result->setMessage();
            result->setOutput(executive->takeOutput().takeBytes());
            result->setGasUsed((int64_t)executive->gasUsed());
            // FIXME: setLogEntries
            // result->setLogEntries(executive->logs());
            result->setNewEVMContractAddress(executive->newAddress());
            executive->callReturnCallback(nullptr, std::move(result));
        }));
}

BlockContext::Ptr TransactionExecutor::createBlockContext(
    const protocol::BlockHeader::ConstPtr& currentHeader, storage::StateStorage::Ptr tableFactory)
{
    (void)m_version;  // TODO: accord to m_version to chose schedule
    BlockContext::Ptr context = make_shared<BlockContext>(tableFactory, m_hashImpl, currentHeader,
        m_executionResultFactory, FiscoBcosScheduleV3, m_pNumberHash, m_isWasm);
    auto tableFactoryPrecompiled =
        std::make_shared<precompiled::TableFactoryPrecompiled>(m_hashImpl);
    tableFactoryPrecompiled->setMemoryTableFactory(tableFactory);
    auto sysConfig = std::make_shared<precompiled::SystemConfigPrecompiled>(m_hashImpl);
    auto parallelConfigPrecompiled =
        std::make_shared<precompiled::ParallelConfigPrecompiled>(m_hashImpl);
    auto consensusPrecompiled = std::make_shared<precompiled::ConsensusPrecompiled>(m_hashImpl);
    auto cnsPrecompiled = std::make_shared<precompiled::CNSPrecompiled>(m_hashImpl);

    context->setAddress2Precompiled(SYS_CONFIG_ADDRESS, sysConfig);
    context->setAddress2Precompiled(TABLE_FACTORY_ADDRESS, tableFactoryPrecompiled);
    context->setAddress2Precompiled(CONSENSUS_ADDRESS, consensusPrecompiled);
    context->setAddress2Precompiled(CNS_ADDRESS, cnsPrecompiled);
    context->setAddress2Precompiled(PARALLEL_CONFIG_ADDRESS, parallelConfigPrecompiled);
    auto kvTableFactoryPrecompiled =
        std::make_shared<precompiled::KVTableFactoryPrecompiled>(m_hashImpl);
    kvTableFactoryPrecompiled->setMemoryTableFactory(tableFactory);
    context->setAddress2Precompiled(KV_TABLE_FACTORY_ADDRESS, kvTableFactoryPrecompiled);
    context->setAddress2Precompiled(
        CRYPTO_ADDRESS, std::make_shared<precompiled::CryptoPrecompiled>(m_hashImpl));
    context->setAddress2Precompiled(
        DAG_TRANSFER_ADDRESS, std::make_shared<precompiled::DagTransferPrecompiled>(m_hashImpl));
    context->setAddress2Precompiled(
        CRYPTO_ADDRESS, std::make_shared<CryptoPrecompiled>(m_hashImpl));
    context->setAddress2Precompiled(
        DEPLOY_WASM_ADDRESS, std::make_shared<DeployWasmPrecompiled>(m_hashImpl));
    context->setAddress2Precompiled(
        CRUD_ADDRESS, std::make_shared<precompiled::CRUDPrecompiled>(m_hashImpl));
    context->setAddress2Precompiled(
        BFS_ADDRESS, std::make_shared<precompiled::FileSystemPrecompiled>(m_hashImpl));
    // context->setAddress2Precompiled(
    //     PERMISSION_ADDRESS, std::make_shared<precompiled::PermissionPrecompiled>());
    // context->setAddress2Precompiled(
    // CONTRACT_LIFECYCLE_ADDRESS, std::make_shared<precompiled::ContractLifeCyclePrecompiled>());
    // context->setAddress2Precompiled(
    //     CHAINGOVERNANCE_ADDRESS, std::make_shared<precompiled::ChainGovernancePrecompiled>());

    // TODO: register User developed Precompiled contract
    // registerUserPrecompiled(context);

    context->setPrecompiledContract(m_precompiledContract);
    // getTxGasLimitToContext from precompiled and set to context
    auto ret = sysConfig->getSysConfigByKey(ledger::SYSTEM_KEY_TX_GAS_LIMIT, tableFactory);
    context->setTxGasLimit(boost::lexical_cast<uint64_t>(ret.first));
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
            else
            {
                return nullptr;
            }
        }
        else
        {
            uint32_t selector = precompiled::getParamFunc(_tx->input());

            auto receiveAddress = _tx->to();
            std::shared_ptr<precompiled::ParallelConfig> config = nullptr;
            // hit the cache, fetch ParallelConfig from the cache directly
            // Note: Only when initializing DAG, get ParallelConfig, will not get ParallelConfig
            // during transaction execution
            auto parallelKey = std::make_pair(string(receiveAddress), selector);
            if (context->getParallelConfigCache()->count(parallelKey))
            {
                config = context->getParallelConfigCache()->at(parallelKey);
            }
            else
            {
                config = parallelConfigPrecompiled->getParallelConfig(
                    context, receiveAddress, selector, _tx->sender());
                context->getParallelConfigCache()->insert(std::make_pair(parallelKey, config));
            }

            if (config == nullptr)
            {
                return nullptr;
            }
            else
            {
                // Testing code
                auto res = make_shared<vector<string>>();

                codec::abi::ABIFunc af;
                bool isOk = af.parser(config->functionName);
                if (!isOk)
                {
                    EXECUTOR_LOG(DEBUG)
                        << LOG_DESC("[getTxCriticals] parser function signature failed, ")
                        << LOG_KV("func signature", config->functionName);

                    return nullptr;
                }

                auto paramTypes = af.getParamsType();
                if (paramTypes.size() < (size_t)config->criticalSize)
                {
                    EXECUTOR_LOG(DEBUG)
                        << LOG_DESC("[getTxCriticals] params type less than  criticalSize")
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
        }
    });
    return context;
}

protocol::BlockNumber TransactionExecutor::getLatestBlockNumberFromStorage()
{
    // use ledger to get latest number
    promise<protocol::BlockNumber> prom;
    m_ledger->asyncGetBlockNumber(
        [&prom](Error::Ptr, protocol::BlockNumber _number) { prom.set_value(_number); });
    return prom.get_future().get();
}

protocol::BlockHeader::Ptr TransactionExecutor::getLatestHeaderFromStorage()
{
    auto currentNumber = getLatestBlockNumberFromStorage();
    // use ledger to get lastest header
    promise<protocol::BlockHeader::Ptr> latestHeaderProm;
    m_ledger->asyncGetBlockDataByNumber(currentNumber, ledger::HEADER,
        [&latestHeaderProm, currentNumber](Error::Ptr error, protocol::Block::Ptr block) {
            if (error)
            {
                EXECUTOR_LOG(FATAL) << LOG_DESC("getLatestHeaderFromStorage failed")
                                    << LOG_KV("blockNumber", currentNumber)
                                    << LOG_KV("message", error->errorMessage());
            }
            latestHeaderProm.set_value(block->blockHeader());
        });
    return latestHeaderProm.get_future().get();
}
