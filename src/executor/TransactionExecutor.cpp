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
#include "interfaces/executor/ExecutionParams.h"
#include "interfaces/storage/StorageInterface.h"
#include <tbb/parallel_for.h>
#include <boost/exception/detail/exception_ptr.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/throw_exception.hpp>
#include <exception>
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
    storage::MergeableStorageInterface::Ptr cacheStorage,
    protocol::ExecutionResultFactory::Ptr executionResultFactory, bcos::crypto::Hash::Ptr hashImpl,
    bool isWasm, size_t poolSize)
  : m_txpool(std::move(txpool)),
    m_backendStorage(std::move(backendStorage)),
    m_cacheStorage(std::move(cacheStorage)),
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

    m_threadPool = std::make_shared<ThreadPool>("asyncTasks", poolSize);
}

void TransactionExecutor::nextBlockHeader(const protocol::BlockHeader::ConstPtr& blockHeader,
    std::function<void(bcos::Error::Ptr&&)> callback) noexcept
{
    try
    {
        bcos::storage::StateStorage::Ptr stateStorage;
        if (m_stateStorages.empty())
        {
            stateStorage = std::make_shared<bcos::storage::StateStorage>(
                m_cacheStorage, m_hashImpl, blockHeader->number());
        }
        else
        {
            auto prev = m_stateStorages.back();
            stateStorage = std::make_shared<bcos::storage::StateStorage>(
                std::move(prev), m_hashImpl, blockHeader->number());
        }

        m_blockContext = std::make_shared<BlockContext>(stateStorage, m_hashImpl, blockHeader,
            m_executionResultFactory, EVMSchedule(), m_isWasm);
        m_stateStorages.push_back(std::move(stateStorage));
        callback(nullptr);
    }
    catch (std::exception& e)
    {
        EXECUTOR_LOG(ERROR) << "Unknown error" << boost::diagnostic_information(e);

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
    asyncExecute(input, true, std::move(callback));
}

void TransactionExecutor::executeTransaction(const protocol::ExecutionParams::ConstPtr& input,
    std::function<void(Error::Ptr&&, protocol::ExecutionResult::Ptr&&)> callback) noexcept
{
    asyncExecute(input, false, std::move(callback));
}

void TransactionExecutor::getTableHashes(bcos::protocol::BlockNumber number,
    std::function<void(
        bcos::Error::Ptr&&, std::vector<std::tuple<std::string, crypto::HashType>>&&)>
        callback) noexcept
{
    EXECUTOR_LOG(INFO) << "getTableHashes" << LOG_KV("number", number);
    if (m_stateStorages.empty())
    {
        EXECUTOR_LOG(ERROR) << "getTableHashes error: No uncommited state in executor";
        callback(BCOS_ERROR_PTR(-1, "No uncommited state in executor"),
            std::vector<std::tuple<std::string, crypto::HashType>>());
        return;
    }

    auto last = m_stateStorages.front();
    if (last->blockNumber() != number)
    {
        auto errorMessage = "getTableHashes error: Request block number: " +
                            boost::lexical_cast<std::string>(number) +
                            " not equal to last blockNumber: " +
                            boost::lexical_cast<std::string>(last->blockNumber());

        EXECUTOR_LOG(ERROR) << errorMessage;
        callback(BCOS_ERROR_PTR(-1, errorMessage),
            std::vector<std::tuple<std::string, crypto::HashType>>());

        return;
    }

    auto tableHashes = last->tableHashes();
    EXECUTOR_LOG(INFO) << "getTableHashes success" << LOG_KV("size", tableHashes.size());

    callback(nullptr, std::move(tableHashes));
}

void TransactionExecutor::prepare(
    const TwoPCParams& params, std::function<void(bcos::Error::Ptr&&)> callback) noexcept
{
    EXECUTOR_LOG(INFO) << "Prepare" << LOG_KV("params", params.number);
    if (m_stateStorages.empty())
    {
        EXECUTOR_LOG(ERROR) << "Prepare error: No uncommited state in executor";
        callback(BCOS_ERROR_PTR(-1, "No uncommited state in executor"));
        return;
    }

    auto last = m_stateStorages.front();
    if (last->blockNumber() != params.number)
    {
        auto errorMessage = "Prepare error: Request block number: " +
                            boost::lexical_cast<std::string>(params.number) +
                            " not equal to last blockNumber: " +
                            boost::lexical_cast<std::string>(last->blockNumber());

        EXECUTOR_LOG(ERROR) << errorMessage;
        callback(BCOS_ERROR_PTR(-1, errorMessage));

        return;
    }

    bcos::storage::TransactionalStorageInterface::TwoPCParams storageParams;
    storageParams.number = params.number;
    m_backendStorage->asyncPrepare(storageParams, last, std::move(callback));
}

void TransactionExecutor::commit(
    const TwoPCParams& params, std::function<void(bcos::Error::Ptr&&)> callback) noexcept
{}

void TransactionExecutor::rollback(
    const TwoPCParams& params, std::function<void(bcos::Error::Ptr&&)> callback) noexcept
{}

void TransactionExecutor::asyncExecute(const bcos::protocol::ExecutionParams::ConstPtr& input,
    bool staticCall,
    std::function<void(bcos::Error::Ptr&&, bcos::protocol::ExecutionResult::Ptr&&)> callback)
{
    auto processExecutive =
        [this, staticCall](TransactionExecutive::Ptr executive,
            std::function<void(bcos::Error::Ptr&&, bcos::protocol::ExecutionResult::Ptr &&)>
                callback) {
            auto contextID = executive->getContextID();
            auto contractAddress = executive->contractAddress();

            auto executive = m_blockContext->getExecutive(contextID, contractAddress);
        };

    switch (input->type())
    {
    case bcos::protocol::ExecutionParams::TXHASH:
    {
        // Get transaction first
        auto txhashes = std::make_shared<bcos::crypto::HashList>();
        txhashes->push_back(input->transactionHash());
        m_txpool->asyncFillBlock(std::move(txhashes),
            [this, input, staticCall, hash = input->transactionHash(), callback,
                processExecutive = std::move(processExecutive)](
                Error::Ptr error, bcos::protocol::TransactionsPtr transactons) {
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
                auto executive = createExecutive(input, staticCall);

                processExecutive(std::move(executive), std::move(callback));
            });

        break;
    }
    case bcos::protocol::ExecutionParams::EXTERNAL_CALL:
    case bcos::protocol::ExecutionParams::EXTERNAL_RETURN:
    {
        auto executive = createExecutive(input, staticCall);

        processExecutive(std::move(executive), callback);
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

std::shared_ptr<TransactionExecutive> TransactionExecutor::createExecutive(
    const protocol::ExecutionParams::ConstPtr& input, bool staticCall)
{
    std::string contract;
    if (input->to().empty())
    {
        if (input->createSalt())
        {  // input->createSalt() is not empty use create2
            contract = newEVMAddress(input->from(), input->input(), input->createSalt().value());
        }
        else
        {
            contract = newEVMAddress(input->from());
        }
    }
    else
    {
        contract = input->to();
    }

    CallParameters callParameters(std::string(input->from()), contract, contract,
        std::string(input->origin()), input->gasAvailable(), input->input(), staticCall);

    auto executive = std::make_shared<TransactionExecutive>(
        m_blockContext, std::move(callParameters), input->contextID(), depth);

    return executive;
}

BlockContext::Ptr TransactionExecutor::createBlockContext(
    const protocol::BlockHeader::ConstPtr& currentHeader, storage::StateStorage::Ptr tableFactory)
{
    (void)m_version;  // TODO: accord to m_version to chose schedule
    BlockContext::Ptr context = make_shared<BlockContext>(tableFactory, m_hashImpl, currentHeader,
        m_executionResultFactory, FiscoBcosScheduleV3, m_isWasm);
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

    // context->setPrecompiledContract(m_precompiledContract);

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
    //         // Note: Only when initializing DAG, get ParallelConfig, will not get ParallelConfig
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
    //             context->getParallelConfigCache()->insert(std::make_pair(parallelKey, config));
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

std::string TransactionExecutor::newEVMAddress(const std::string_view&)
{
    // TODO design a new address
    // u256 nonce = m_s->getNonce(_sender);
    // auto hash = m_hashImpl->hash(string(_sender) + nonce.str());
    // return string((char*)hash.data(), 20);
    return "address!";
}

std::string TransactionExecutor::newEVMAddress(
    const std::string_view& _sender, bytesConstRef _init, u256 const& _salt)
{
    auto hash = m_hashImpl->hash(
        bytes{0xff} + toBytes(_sender) + toBigEndian(_salt) + m_hashImpl->hash(_init));
    return string((char*)hash.data(), 20);
}