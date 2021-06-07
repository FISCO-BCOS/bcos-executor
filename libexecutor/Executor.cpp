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
 * @brief block level context
 * @file ExecutiveContext.h
 * @author: xingqiangbai
 * @date: 2021-05-27
 */
#include "Executor.h"
#include "../libprecompiled/Utilities.h"
#include "../libstate/State.h"
#include "../libvm/Executive.h"
#include "../libvm/ExecutiveContext.h"
#include "../libvm/Precompiled.h"
#include "../libvm/PrecompiledContract.h"
#include "Common.h"
#include "TxDAG.h"
#include "bcos-framework/interfaces/protocol/TransactionReceipt.h"
#include "bcos-framework/libtable/Table.h"
#include "bcos-framework/libtable/TableFactory.h"
#include <tbb/parallel_for.h>
#include <exception>
#include <thread>
// #include "include/UserPrecompiled.h"
#include "../libprecompiled/CNSPrecompiled.h"
// #include "../libprecompiled/CRUDPrecompiled.h"
// #include "../libprecompiled/ChainGovernancePrecompiled.h"
#include "../libprecompiled/ConsensusPrecompiled.h"
// #include "../libprecompiled/ContractLifeCyclePrecompiled.h"
#include "../libprecompiled/KVTableFactoryPrecompiled.h"
#include "../libprecompiled/ParallelConfigPrecompiled.h"
// #include "../libprecompiled/PermissionPrecompiled.h"
#include "../libprecompiled/CryptoPrecompiled.h"
#include "../libprecompiled/PrecompiledResult.h"
#include "../libprecompiled/SystemConfigPrecompiled.h"
#include "../libprecompiled/TableFactoryPrecompiled.h"
// #include "../libprecompiled/WorkingSealerManagerPrecompiled.h"
#include "../libprecompiled/extension/DagTransferPrecompiled.h"
#include "../libvm/ExecutiveContext.h"
#include "bcos-framework/libcodec/abi/ContractABIType.h"

using namespace bcos;
using namespace std;
using namespace bcos::executor;
using namespace bcos::protocol;
using namespace bcos::storage;
using namespace bcos::precompiled;

Executor::Executor(const protocol::BlockFactory::Ptr& _blockFactory,
    const ledger::LedgerInterface::Ptr& _ledger,
    const storage::StorageInterface::Ptr& _stateStorage, bool _isWasm)
  : m_blockFactory(_blockFactory),
    m_ledger(_ledger),
    m_stateStorage(_stateStorage),
    m_isWasm(_isWasm)
{
    m_threadNum = std::max(std::thread::hardware_concurrency(), (unsigned int)1);
    m_hashImpl = m_blockFactory->cryptoSuite()->hashImpl();
    // FIXME: CallBackFunction convert ledger asyncGetBlockHashByNumber to sync
    m_precompiledContract.insert(std::make_pair(std::string("0x1"),
        make_shared<PrecompiledContract>(3000, 0, PrecompiledRegistrar::executor("ecrecover"))));
    m_precompiledContract.insert(std::make_pair(
        std::string("0x2"), make_shared<PrecompiledContract>(60, 12, PrecompiledRegistrar::executor("sha256"))));
    m_precompiledContract.insert(std::make_pair(std::string("0x3"),
        make_shared<PrecompiledContract>(600, 120, PrecompiledRegistrar::executor("ripemd160"))));
    m_precompiledContract.insert(std::make_pair(std::string("0x4"),
        make_shared<PrecompiledContract>(15, 3, PrecompiledRegistrar::executor("identity"))));
    m_precompiledContract.insert(
        {std::string("0x5"), make_shared<PrecompiledContract>(PrecompiledRegistrar::pricer("modexp"),
                                 PrecompiledRegistrar::executor("modexp"))});
    m_precompiledContract.insert({std::string("0x6"),
        make_shared<PrecompiledContract>(150, 0, PrecompiledRegistrar::executor("alt_bn128_G1_add"))});
    m_precompiledContract.insert({std::string("0x7"),
        make_shared<PrecompiledContract>(6000, 0, PrecompiledRegistrar::executor("alt_bn128_G1_mul"))});
    m_precompiledContract.insert({std::string("0x8"),
        make_shared<PrecompiledContract>(PrecompiledRegistrar::pricer("alt_bn128_pairing_product"),
            PrecompiledRegistrar::executor("alt_bn128_pairing_product"))});
    m_precompiledContract.insert(
        {std::string("0x9"), make_shared<PrecompiledContract>(PrecompiledRegistrar::pricer("blake2_compression"),
                                 PrecompiledRegistrar::executor("blake2_compression"))});
}

void Executor::asyncGetCode(std::shared_ptr<std::string> _address,
    std::function<void(const Error::Ptr&, const std::shared_ptr<bytes>&)> _callback)
{
    (void)_address;
    (void)_callback;
}

void Executor::asyncExecuteTransaction(const protocol::Transaction::ConstPtr& _tx,
    std::function<void(const Error::Ptr&, const protocol::TransactionReceipt::ConstPtr&)> _callback)
{
    // FIXME: fake a block info to execute transaction
    ExecutiveContext::Ptr executiveContext = createExecutiveContext(nullptr);
    auto executive = std::make_shared<Executive>(executiveContext);
    // only Rpc::call will use executeTransaction, RPC do catch exception
    auto receipt = executeTransaction(_tx, executiveContext, executive);
    _callback(nullptr, receipt);
    // FIXME: make this interface async
}

ExecutiveContext::Ptr Executor::executeBlock(
    const protocol::Block::Ptr& block, const protocol::BlockHeader::Ptr& parentBlockInfo)

{
    EXECUTOR_LOG(INFO) << LOG_DESC("[executeBlock]Executing block")
                       << LOG_KV("txNum", block->transactionsSize())
                       << LOG_KV("num", block->blockHeader()->number())
                       << LOG_KV("parentHash", parentBlockInfo->hash())
                       << LOG_KV("parentNum", parentBlockInfo->number())
                       << LOG_KV("parentStateRoot", parentBlockInfo->stateRoot());
    // return nullptr prepare to exit when m_stop is true
    if (m_stop.load())
    {
        return nullptr;
    }
    auto start_time = utcTime();
    auto record_time = utcTime();
    ExecutiveContext::Ptr executiveContext = createExecutiveContext(block->blockHeader());

    auto initExeCtx_time_cost = utcTime() - record_time;
    record_time = utcTime();
    auto tempReceiptRoot = block->blockHeader()->receiptRoot();
    auto tempStateRoot = block->blockHeader()->stateRoot();
    auto tempHeaderHash = block->blockHeader()->hash();
    // FIXME: check logic below
    // block->clearAllReceipts();
    // block->resizeTransactionReceipt(block->transactionsSize());
    auto perpareBlock_time_cost = utcTime() - record_time;
    record_time = utcTime();

    shared_ptr<TxDAG> txDag = make_shared<TxDAG>();
    txDag->init(executiveContext, block);

    txDag->setTxExecuteFunc([&](Transaction::ConstPtr _tr, ID _txId, Executive::Ptr _executive) {
        auto resultReceipt = executeTransaction(_tr, executiveContext, _executive);

        block->setReceipt(_txId, resultReceipt);
        executiveContext->getState()->commit();
        return true;
    });
    auto initDag_time_cost = utcTime() - record_time;
    record_time = utcTime();

    auto parallelTimeOut = utcSteadyTime() + 30000;  // 30 timeout

    try
    {
        tbb::atomic<bool> isWarnedTimeout(false);
        tbb::parallel_for(tbb::blocked_range<unsigned int>(0, m_threadNum),
            [&](const tbb::blocked_range<unsigned int>& _r) {
                (void)_r;
                auto executive = std::make_shared<Executive>(executiveContext);

                while (!txDag->hasFinished())
                {
                    if (!isWarnedTimeout.load() && utcSteadyTime() >= parallelTimeOut)
                    {
                        isWarnedTimeout.store(true);
                        EXECUTOR_LOG(WARNING)
                            << LOG_BADGE("executeBlock") << LOG_DESC("Para execute block timeout")
                            << LOG_KV("txNum", block->transactionsSize())
                            << LOG_KV("blockNumber", block->blockHeader()->number());
                    }

                    txDag->executeUnit(executive);
                }
            });
    }
    catch (exception& e)
    {
        EXECUTOR_LOG(ERROR) << LOG_BADGE("executeBlock")
                            << LOG_DESC("Error during parallel block execution")
                            << LOG_KV("EINFO", boost::diagnostic_information(e));

        BOOST_THROW_EXCEPTION(
            BlockExecutionFailed() << errinfo_comment("Error during parallel block execution"));
    }
    // if the program is going to exit, return nullptr directly
    if (m_stop.load())
    {
        return nullptr;
    }
    auto exe_time_cost = utcTime() - record_time;
    record_time = utcTime();

    h256 stateRoot = executiveContext->getState()->rootHash();
    auto getRootHash_time_cost = utcTime() - record_time;
    record_time = utcTime();

    // FIXME: check logic below
    // block->setStateRootToAllReceipt(stateRoot);
    // block->updateSequenceReceiptGas();
    auto setAllReceipt_time_cost = utcTime() - record_time;
    record_time = utcTime();

    block->calculateReceiptRoot(true);
    auto getReceiptRoot_time_cost = utcTime() - record_time;
    record_time = utcTime();

    // FIXME: stateRoot = executiveContext->getTableFactory()->hash()
    block->blockHeader()->setStateRoot(stateRoot);
    // block->blockHeader()->setDBhash(executiveContext->getTableFactory()->hash());

    auto setStateRoot_time_cost = utcTime() - record_time;
    record_time = utcTime();
    // Consensus module execute block, receiptRoot is empty, skip this judgment
    // The sync module execute block, receiptRoot is not empty, need to compare BlockHeader
    if (tempReceiptRoot != h256())
    {
        if (tempHeaderHash != block->blockHeader()->hash())
        {
            EXECUTOR_LOG(ERROR) << "Invalid Block with bad stateRoot or receiptRoot"
                                << LOG_KV("blkNum", block->blockHeader()->number())
                                << LOG_KV("Hash", tempHeaderHash.abridged())
                                << LOG_KV("myHash", block->blockHeader()->hash().abridged())
                                << LOG_KV("Receipt", tempReceiptRoot.abridged())
                                << LOG_KV(
                                       "myRecepit", block->blockHeader()->receiptRoot().abridged())
                                << LOG_KV("State", tempStateRoot.abridged())
                                << LOG_KV("myState", block->blockHeader()->stateRoot().abridged());
            BOOST_THROW_EXCEPTION(InvalidBlockWithBadRoot() << errinfo_comment(
                                      "Invalid Block with bad stateRoot or ReciptRoot"));
        }
    }
    EXECUTOR_LOG(DEBUG) << LOG_BADGE("executeBlock") << LOG_DESC("Para execute block takes")
                        << LOG_KV("time(ms)", utcTime() - start_time)
                        << LOG_KV("txNum", block->transactionsSize())
                        << LOG_KV("blockNumber", block->blockHeader()->number())
                        << LOG_KV("blockHash", block->blockHeader()->hash())
                        << LOG_KV("stateRoot", block->blockHeader()->stateRoot())
                        << LOG_KV("transactionRoot", block->blockHeader()->txsRoot())
                        << LOG_KV("receiptRoot", block->blockHeader()->receiptRoot())
                        << LOG_KV("initExeCtxTimeCost", initExeCtx_time_cost)
                        << LOG_KV("perpareBlockTimeCost", perpareBlock_time_cost)
                        << LOG_KV("initDagTimeCost", initDag_time_cost)
                        << LOG_KV("exeTimeCost", exe_time_cost)
                        << LOG_KV("getRootHashTimeCost", getRootHash_time_cost)
                        << LOG_KV("setAllReceiptTimeCost", setAllReceipt_time_cost)
                        << LOG_KV("getReceiptRootTimeCost", getReceiptRoot_time_cost)
                        << LOG_KV("setStateRootTimeCost", setStateRoot_time_cost);
    return executiveContext;
}

protocol::TransactionReceipt::Ptr Executor::executeTransaction(protocol::Transaction::ConstPtr _t,
    executor::ExecutiveContext::Ptr executiveContext, Executive::Ptr executive)
{
    (void)executiveContext;
    // Create and initialize the executive. This will throw fairly cheaply and quickly if the
    // transaction is bad in any way.
    executive->reset();

    // OK - transaction looks valid - execute.
    try
    {
        executive->initialize(_t);
        if (!executive->execute())
            executive->go();
        executive->finalize();
    }
    // catch (StorageException const& e)
    // {
    //     EXECUTOR_LOG(ERROR) << LOG_DESC("get StorageException") << LOG_KV("what", e.what());
    //     BOOST_THROW_EXCEPTION(e);
    // }
    catch (Exception const& _e)
    {
        // only OutOfGasBase ExecutorNotFound exception will throw
        EXECUTOR_LOG(ERROR) << diagnostic_information(_e);
    }
    catch (std::exception const& _e)
    {
        EXECUTOR_LOG(ERROR) << _e.what();
    }

    executive->loggingException();
    // FIXME: use the true transactionReceipt, use receiptFactory to create
    // FIXME: the receiptFactory from blockFactory

    // return std::make_shared<TransactionReceipt>(executiveContext->getState()->rootHash(false),
    //     executive->gasUsed(), executive->logs(), executive->status(),
    //     executive->takeOutput().takeBytes(), executive->newAddress());
    return nullptr;
}

ExecutiveContext::Ptr Executor::createExecutiveContext(
    const protocol::BlockHeader::Ptr& currentHeader)
{
    // FIXME: if wasm use the SYS_CONFIG_NAME as address
    // FIXME: TableFactory maybe need sa member to continues execute block without write to DB
    auto tableFactory =
        std::make_shared<TableFactory>(m_stateStorage, m_hashImpl, currentHeader->number());
    ExecutiveContext::Ptr context = make_shared<ExecutiveContext>(
        tableFactory, m_hashImpl, currentHeader, m_pNumberHash, m_isWasm);
    auto tableFactoryPrecompiled = std::make_shared<precompiled::TableFactoryPrecompiled>();
    tableFactoryPrecompiled->setMemoryTableFactory(tableFactory);
    context->setAddress2Precompiled(
        SYS_CONFIG_ADDRESS, std::make_shared<precompiled::SystemConfigPrecompiled>());
    context->setAddress2Precompiled(TABLE_FACTORY_ADDRESS, tableFactoryPrecompiled);
    // context->setAddress2Precompiled(CRUD_ADDRESS,
    // std::make_shared<precompiled::CRUDPrecompiled>());
    context->setAddress2Precompiled(
        CONSENSUS_ADDRESS, std::make_shared<precompiled::ConsensusPrecompiled>());
    context->setAddress2Precompiled(CNS_ADDRESS, std::make_shared<precompiled::CNSPrecompiled>());
    // context->setAddress2Precompiled(
    //     PERMISSION_ADDRESS, std::make_shared<precompiled::PermissionPrecompiled>());

    auto parallelConfigPrecompiled = std::make_shared<precompiled::ParallelConfigPrecompiled>();
    context->setAddress2Precompiled(PARALLEL_CONFIG_ADDRESS, parallelConfigPrecompiled);
    // context->setAddress2Precompiled(
    // CONTRACT_LIFECYCLE_ADDRESS, std::make_shared<precompiled::ContractLifeCyclePrecompiled>());
    auto kvTableFactoryPrecompiled = std::make_shared<precompiled::KVTableFactoryPrecompiled>();
    kvTableFactoryPrecompiled->setMemoryTableFactory(tableFactory);
    context->setAddress2Precompiled(KV_TABLE_FACTORY_ADDRESS, kvTableFactoryPrecompiled);
    // context->setAddress2Precompiled(
    //     CHAINGOVERNANCE_ADDRESS, std::make_shared<precompiled::ChainGovernancePrecompiled>());

    // FIXME: register User developed Precompiled contract
    // registerUserPrecompiled(context);

    context->setPrecompiledContract(m_precompiledContract);
    auto state = make_shared<State>(tableFactory, m_hashImpl);
    context->setState(state);

    // FIXME: setTxGasLimitToContext
    // setTxGasLimitToContext(context);

    // register workingSealerManagerPrecompiled for VRF-based-rPBFT

    // context->setAddress2Precompiled(WORKING_SEALER_MGR_ADDRESS,
    //     std::make_shared<precompiled::WorkingSealerManagerPrecompiled>());
    context->setAddress2Precompiled(CRYPTO_ADDRESS, std::make_shared<CryptoPrecompiled>());
    context->setTxCriticalsHandler([&](const protocol::Transaction::ConstPtr& _tx)
                                       -> std::shared_ptr<std::vector<std::string>> {
        if (_tx->type() == protocol::TransactionType::ContractCreation)
        {
            // Not to parallel contract creation transaction
            return nullptr;
        }

        auto p = context->getPrecompiled(_tx->to().toString());
        if (p)
        {
            // Precompile transaction
            if (p->isParallelPrecompiled())
            {
                auto ret = make_shared<vector<string>>(p->getParallelTag(_tx->input()));
                for (string& critical : *ret)
                {
                    critical += _tx->to().toString();
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

            auto receiveAddress = _tx->to().toString();
            std::shared_ptr<precompiled::ParallelConfig> config = nullptr;
            // hit the cache, fetch ParallelConfig from the cache directly
            // Note: Only when initializing DAG, get ParallelConfig, will not get ParallelConfig
            // during transaction execution
            auto parallelKey = std::make_pair(receiveAddress, selector);
            config = parallelConfigPrecompiled->getParallelConfig(
                context, receiveAddress, selector, _tx->sender().toString());

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
                    critical += _tx->to().toString();
                }

                return res;
            }
        }
    });
    return context;
}
