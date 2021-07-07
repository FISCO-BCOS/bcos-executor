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
 * @file BlockContext.h
 * @author: xingqiangbai
 * @date: 2021-05-27
 */
#include "Executor.h"
#include "../libprecompiled/CNSPrecompiled.h"
#include "../libprecompiled/ConsensusPrecompiled.h"
#include "../libprecompiled/CryptoPrecompiled.h"
#include "../libprecompiled/KVTableFactoryPrecompiled.h"
#include "../libprecompiled/ParallelConfigPrecompiled.h"
#include "../libprecompiled/PrecompiledResult.h"
#include "../libprecompiled/SystemConfigPrecompiled.h"
#include "../libprecompiled/TableFactoryPrecompiled.h"
#include "../libprecompiled/Utilities.h"
#include "../libprecompiled/extension/DagTransferPrecompiled.h"
#include "../libstate/State.h"
#include "../libvm/BlockContext.h"
#include "../libvm/Executive.h"
#include "../libvm/Precompiled.h"
#include "Common.h"
#include "TxDAG.h"
#include "bcos-framework/interfaces/protocol/TransactionReceipt.h"
#include "bcos-framework/libcodec/abi/ContractABIType.h"
#include "bcos-framework/libtable/Table.h"
#include "bcos-framework/libtable/TableFactory.h"
#include "bcos-framework/libutilities/ThreadPool.h"
#include <tbb/parallel_for.h>
#include <exception>
#include <thread>
// #include "include/UserPrecompiled.h"
// #include "../libprecompiled/CRUDPrecompiled.h"
// #include "../libprecompiled/PermissionPrecompiled.h"
// #include "../libprecompiled/ChainGovernancePrecompiled.h"
// #include "../libprecompiled/ContractLifeCyclePrecompiled.h"
// #include "../libprecompiled/WorkingSealerManagerPrecompiled.h"

using namespace bcos;
using namespace std;
using namespace bcos::executor;
using namespace bcos::protocol;
using namespace bcos::storage;
using namespace bcos::precompiled;

inline std::ostream& operator<<(std::ostream& _out, const TransactionReceipt::Ptr& _r)
{
    _out << "Version: " << _r->version() << "\n";
    _out << "Number: " << _r->blockNumber() << "\n";
    _out << "Hash: " << _r->hash() << "\n";
    _out << "Gas used: " << _r->gasUsed() << "\n";
    _out << "contractAddress : " << _r->contractAddress().toString() << "\n";
    _out << "status : " << int(_r->status()) << "\n";
    _out << "output: " << _r->output().toString() << " \n";
    _out << "Logs: " << _r->logEntries().size() << " entries:"
         << "\n";
    _out << "Bloom: " << _r->bloom() << "\n";
    return _out;
}

Executor::Executor(const protocol::BlockFactory::Ptr& _blockFactory,
    const dispatcher::DispatcherInterface::Ptr& _dispatcher,
    const ledger::LedgerInterface::Ptr& _ledger,
    const storage::StorageInterface::Ptr& _stateStorage, bool _isWasm, size_t _poolSize)
  : m_blockFactory(_blockFactory),
    m_dispatcher(_dispatcher),
    m_ledger(_ledger),
    m_stateStorage(_stateStorage),
    m_isWasm(_isWasm),
    m_version(Version_3_0_0)  // current executor version, will set as new block's version
{
    assert(m_blockFactory);
    assert(m_dispatcher);
    assert(m_ledger);
    assert(m_stateStorage);
    m_threadNum = std::max(std::thread::hardware_concurrency(), (unsigned int)1);
    m_hashImpl = m_blockFactory->cryptoSuite()->hashImpl();
    m_precompiledContract.insert(std::make_pair(std::string("0x1"),
        make_shared<PrecompiledContract>(3000, 0, PrecompiledRegistrar::executor("ecrecover"))));
    m_precompiledContract.insert(std::make_pair(std::string("0x2"),
        make_shared<PrecompiledContract>(60, 12, PrecompiledRegistrar::executor("sha256"))));
    m_precompiledContract.insert(std::make_pair(std::string("0x3"),
        make_shared<PrecompiledContract>(600, 120, PrecompiledRegistrar::executor("ripemd160"))));
    m_precompiledContract.insert(std::make_pair(std::string("0x4"),
        make_shared<PrecompiledContract>(15, 3, PrecompiledRegistrar::executor("identity"))));
    m_precompiledContract.insert({std::string("0x5"),
        make_shared<PrecompiledContract>(
            PrecompiledRegistrar::pricer("modexp"), PrecompiledRegistrar::executor("modexp"))});
    m_precompiledContract.insert(
        {std::string("0x6"), make_shared<PrecompiledContract>(
                                 150, 0, PrecompiledRegistrar::executor("alt_bn128_G1_add"))});
    m_precompiledContract.insert(
        {std::string("0x7"), make_shared<PrecompiledContract>(
                                 6000, 0, PrecompiledRegistrar::executor("alt_bn128_G1_mul"))});
    m_precompiledContract.insert({std::string("0x8"),
        make_shared<PrecompiledContract>(PrecompiledRegistrar::pricer("alt_bn128_pairing_product"),
            PrecompiledRegistrar::executor("alt_bn128_pairing_product"))});
    m_precompiledContract.insert({std::string("0x9"),
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
    m_threadPool = std::make_shared<bcos::ThreadPool>("asyncTasks", _poolSize);
    // use ledger to get lastest header
    m_lastHeader = getLatestHeaderFromStorage();
    m_tableFactory =
        std::make_shared<TableFactory>(m_stateStorage, m_hashImpl, m_lastHeader->number() + 1);
}

void Executor::start()
{
    m_stop.store(false);

    m_worker = make_unique<thread>([&]() {
        EXECUTOR_LOG(INFO) << LOG_DESC("started");
        while (!m_stop.load())
        {
            std::promise<protocol::Block::Ptr> prom;
            m_dispatcher->asyncGetLatestBlock(
                [&prom](const Error::Ptr& error, const protocol::Block::Ptr& block) {
                    if (error)
                    {
                        EXECUTOR_LOG(WARNING) << LOG_DESC("asyncGetLatestBlock failed")
                                              << LOG_KV("message", error->errorMessage());
                    }
                    prom.set_value(block);
                });
            auto currentBlock = prom.get_future().get();
            if (!m_lastHeader)
            {
                m_lastHeader = getLatestHeaderFromStorage();
                m_tableFactory = std::make_shared<TableFactory>(
                    m_stateStorage, m_hashImpl, m_lastHeader->number() + 1);
            }
            // check the current block's number == m_lastHeader number + 1
            if (currentBlock->blockHeader()->number() != m_lastHeader->number() + 1)
            {
                EXECUTOR_LOG(ERROR) << LOG_DESC("check BlockNumber continuity failed")
                                    << LOG_KV("expect", m_lastHeader->number() + 1)
                                    << LOG_KV("got", currentBlock->blockHeader()->number());
                // TODO: maybe process return error?
                resultNotifier(make_shared<Error>(ExecutorErrorCode::DiscontinuousBlockNumber,
                                   "check BlockNumber continuity failed"),
                    m_blockFactory->blockHeaderFactory()->createBlockHeader(
                        currentBlock->blockHeader()->number()));
                m_lastHeader = nullptr;
                // the continuity of blockNumber is broken, clear executor's state
                m_tableFactory = nullptr;
                continue;
            }
            // execute current block
            BlockContext::Ptr context = nullptr;
            try
            {
                context = executeBlock(currentBlock);
            }
            catch (exception& e)
            {
                EXECUTOR_LOG(ERROR) << LOG_DESC("executeBlock failed")
                                    << LOG_KV("message", boost::diagnostic_information(e));
                // TODO: maybe process return error?
                resultNotifier(make_shared<Error>(ExecutorErrorCode::ExecuteException,
                                   boost::diagnostic_information(e)),
                    m_blockFactory->blockHeaderFactory()->createBlockHeader(
                        currentBlock->blockHeader()->number()));
                m_lastHeader = nullptr;
                m_tableFactory = nullptr;
                continue;
            }

            // copy TableFactory for next block
            auto latestBlockNumberOfStorage = getLatestBlockNumberFromStorage();
            auto data = context->getTableFactory()->exportData(latestBlockNumberOfStorage);
            // use ledger to commit receipts
            std::promise<Error::Ptr> errorProm;
            m_ledger->asyncStoreReceipts(context->getTableFactory(), currentBlock,
                [&errorProm](Error::Ptr error) { errorProm.set_value(error); });
            auto error = errorProm.get_future().get();
            if (error)
            {
                EXECUTOR_LOG(FATAL) << LOG_DESC("asyncStoreReceipts failed")
                                    << LOG_KV("message", error->errorMessage());
                continue;
            }
            error = resultNotifier(nullptr, currentBlock->blockHeader());
            if (!error)
            {
                // set m_lastHeader to current block header
                m_lastHeader = currentBlock->blockHeader();
                // create a new TableFactory and import data with new blocknumber
                m_tableFactory = std::make_shared<TableFactory>(
                    m_stateStorage, m_hashImpl, m_lastHeader->number() + 1);
                // the cache only have some block's state, it is not infinit
                m_tableFactory->importData(data.first, data.second, false);
                EXECUTOR_LOG(DEBUG) << LOG_DESC("asyncNotifyExecutionResult")
                                    << LOG_KV("BlockNumber", m_lastHeader->number());
            }
            else
            {
                EXECUTOR_LOG(ERROR) << LOG_DESC("asyncNotifyExecutionResult failed")
                                    << LOG_KV("message", error->errorMessage());
            }
        }
        EXECUTOR_LOG(INFO) << LOG_DESC("stopped");
    });
}

void Executor::stop()
{
    m_stop.store(true);
    m_threadPool->stop();
}

void Executor::asyncGetCode(const std::string_view& _address,
    std::function<void(const Error::Ptr&, const std::shared_ptr<bytes>&)> _callback)
{  // TODO: make state a member of Executor
    auto state = make_shared<State>(m_tableFactory, m_hashImpl, m_isWasm);
    m_threadPool->enqueue([state, address = string(_address), _callback]() {
        auto code = state->code(address);
        _callback(nullptr, code);
    });
}

void Executor::asyncExecuteTransaction(const protocol::Transaction::ConstPtr& _tx,
    std::function<void(const Error::Ptr&, const protocol::TransactionReceipt::ConstPtr&)> _callback)
{
    // use m_lastHeader to execute transaction
    auto currentHeader = m_blockFactory->blockHeaderFactory()->populateBlockHeader(m_lastHeader);
    BlockContext::Ptr executiveContext = createExecutiveContext(currentHeader);
    auto executive = std::make_shared<Executive>(executiveContext);
    m_threadPool->enqueue([&, executiveContext, executive, _tx, _callback]() {
        // only Rpc::call will use executeTransaction, RPC do catch exception
        auto receipt = executeTransaction(_tx, executive);
        _callback(nullptr, receipt);
    });
}

BlockContext::Ptr Executor::executeBlock(const protocol::Block::Ptr& block)

{
    // return nullptr prepare to exit when m_stop is true
    if (m_stop.load())
    {
        return nullptr;
    }

    auto start_time = utcTime();
    auto record_time = utcTime();
    auto originalHeader = block->blockHeader();
    if (originalHeader->version() == 0)
    {  // if block version is 0, set the block version to current executor version
        originalHeader->setVersion(m_version);
    }
    // create a new block header to return
    auto currentHeader = m_blockFactory->blockHeaderFactory()->populateBlockHeader(originalHeader);
    BlockContext::Ptr executiveContext = createExecutiveContext(currentHeader);
    EXECUTOR_LOG(INFO) << LOG_DESC("[executeBlock]Executing")
                       << LOG_KV("blockNumber", originalHeader->number())
                       << LOG_KV("txNum", block->transactionsSize())
                       << LOG_KV("parentHash", currentHeader->parentInfo()[0].blockHash)
                       << LOG_KV("parentNum", currentHeader->parentInfo()[0].blockNumber)
                       << LOG_KV("state", originalHeader->stateRoot().abridged())
                       << LOG_KV("receipt", originalHeader->receiptsRoot().abridged());

    auto initExeCtx_time_cost = utcTime() - record_time;
    record_time = utcTime();

    shared_ptr<TxDAG> txDag = make_shared<TxDAG>();
    txDag->init(executiveContext, block);
    mutex blockGasMutex;
    u256 blockGasUsed = 0;
    txDag->setTxExecuteFunc([&](Transaction::ConstPtr _tr, ID _txId, Executive::Ptr _executive) {
        auto resultReceipt = executeTransaction(_tr, _executive);
        block->setReceipt(_txId, resultReceipt);
        {
            lock_guard l(blockGasMutex);
            blockGasUsed += resultReceipt->gasUsed();
        }
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
                            << LOG_KV("blockNumber", currentHeader->number());
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
    // stateRoot same as executiveContext->getTableFactory()->hash()
    auto stateRoot = executiveContext->getState()->rootHash();
    auto getRootHash_time_cost = utcTime() - record_time;
    record_time = utcTime();

    auto receiptRoot = block->calculateReceiptRoot(false);
    currentHeader->setReceiptsRoot(receiptRoot);
    currentHeader->setStateRoot(stateRoot);
    currentHeader->setGasUsed(blockGasUsed);
    auto getReceiptRoot_time_cost = utcTime() - record_time;
    // Consensus module execute block, receiptRoot is empty, skip this judgment
    // The sync module execute block, receiptRoot is not empty, need to compare BlockHeader
    if (originalHeader->stateRoot() != h256() && originalHeader->hash() != currentHeader->hash())
    {
        EXECUTOR_LOG(ERROR) << "Invalid Block with bad stateRoot or receiptRoot"
                            << LOG_KV("blockNumber", currentHeader->number())
                            << LOG_KV("originalHash", originalHeader->hash().abridged())
                            << LOG_KV("currentHash", currentHeader->hash().abridged())
                            << LOG_KV("originalReceipt", originalHeader->receiptsRoot().abridged())
                            << LOG_KV("currentRecepit", currentHeader->receiptsRoot().abridged())
                            << LOG_KV("originalState", originalHeader->stateRoot().abridged())
                            << LOG_KV("currentState", currentHeader->stateRoot().abridged());
        BOOST_THROW_EXCEPTION(InvalidBlockWithBadRoot() << errinfo_comment(
                                  "The correct blockHash is " + originalHeader->hash().abridged()));
#if FISCO_DEBUG
        for (size_t i = 0; i < block->receiptsSize(); ++i)
        {
            EXECUTOR_LOG(DEBUG) << LOG_BADGE("FISCO_DEBUG") << LOG_KV("index", i)
                                << LOG_KV("hash", block->transaction(i)->hash())
                                << ",receipt=" << block->receipt(i);
        }
#endif
    }
    if (originalHeader->stateRoot() == crypto::HashType())
    {  // only consensus block setBlockHeader
        block->setBlockHeader(currentHeader);
    }
    EXECUTOR_LOG(DEBUG) << LOG_BADGE("executeBlock") << LOG_DESC("Para execute block takes")
                        << LOG_KV("blockNumber", currentHeader->number())
                        << LOG_KV("time(ms)", utcTime() - start_time)
                        << LOG_KV("txNum", block->transactionsSize())
                        << LOG_KV("blockHash", currentHeader->hash())
                        << LOG_KV("stateRoot", currentHeader->stateRoot())
                        << LOG_KV("transactionRoot", currentHeader->txsRoot())
                        << LOG_KV("receiptRoot", currentHeader->receiptsRoot())
                        << LOG_KV("initExeCtxTimeCost", initExeCtx_time_cost)
                        << LOG_KV("initDagTimeCost", initDag_time_cost)
                        << LOG_KV("exeTimeCost", exe_time_cost)
                        << LOG_KV("getRootHashTimeCost", getRootHash_time_cost)
                        << LOG_KV("getReceiptRootTimeCost", getReceiptRoot_time_cost);


    return executiveContext;
}

protocol::TransactionReceipt::Ptr Executor::executeTransaction(
    protocol::Transaction::ConstPtr _t, Executive::Ptr executive)
{
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
        EXECUTOR_LOG(ERROR) << boost::diagnostic_information(_e);
    }

    executive->loggingException();
    // use receiptFactory to create the receiptFactory from blockFactory
    auto receiptFactory = m_blockFactory->receiptFactory();
    return receiptFactory->createReceipt(executive->gasUsed(), toBytes(executive->newAddress()),
        executive->logs(), (int32_t)executive->status(), executive->takeOutput().takeBytes(),
        executive->getEnvInfo()->currentNumber());
}

BlockContext::Ptr Executor::createExecutiveContext(const protocol::BlockHeader::Ptr& currentHeader)
{
    // TableFactory is member to continues execute block without write to DB
    (void)m_version;  // TODO: accord to m_version to chose schedule
    BlockContext::Ptr context = make_shared<BlockContext>(
        m_tableFactory, m_hashImpl, currentHeader, FiscoBcosScheduleV3, m_pNumberHash, m_isWasm);
    auto tableFactoryPrecompiled =
        std::make_shared<precompiled::TableFactoryPrecompiled>(m_hashImpl);
    tableFactoryPrecompiled->setMemoryTableFactory(m_tableFactory);
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
    kvTableFactoryPrecompiled->setMemoryTableFactory(m_tableFactory);
    context->setAddress2Precompiled(KV_TABLE_FACTORY_ADDRESS, kvTableFactoryPrecompiled);
    context->setAddress2Precompiled(
        CRYPTO_ADDRESS, std::make_shared<precompiled::CryptoPrecompiled>(m_hashImpl));
    context->setAddress2Precompiled(
        DAG_TRANSFER_ADDRESS, std::make_shared<precompiled::DagTransferPrecompiled>(m_hashImpl));
    context->setAddress2Precompiled(
        CRYPTO_ADDRESS, std::make_shared<CryptoPrecompiled>(m_hashImpl));

    // context->setAddress2Precompiled(CRUD_ADDRESS,
    // std::make_shared<precompiled::CRUDPrecompiled>());
    // context->setAddress2Precompiled(
    //     PERMISSION_ADDRESS, std::make_shared<precompiled::PermissionPrecompiled>());
    // context->setAddress2Precompiled(
    // CONTRACT_LIFECYCLE_ADDRESS, std::make_shared<precompiled::ContractLifeCyclePrecompiled>());
    // context->setAddress2Precompiled(
    //     CHAINGOVERNANCE_ADDRESS, std::make_shared<precompiled::ChainGovernancePrecompiled>());

    // register workingSealerManagerPrecompiled for VRF-based-rPBFT
    // context->setAddress2Precompiled(WORKING_SEALER_MGR_ADDRESS,
    //     std::make_shared<precompiled::WorkingSealerManagerPrecompiled>());

    // TODO: register User developed Precompiled contract
    // registerUserPrecompiled(context);

    context->setPrecompiledContract(m_precompiledContract);
    // getTxGasLimitToContext from precompiled and set to context
    auto ret = sysConfig->getSysConfigByKey(ledger::SYSTEM_KEY_TX_GAS_LIMIT, m_tableFactory);
    context->setTxGasLimit(boost::lexical_cast<uint64_t>(ret.first));
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

protocol::BlockNumber Executor::getLatestBlockNumberFromStorage()
{
    // use ledger to get latest number
    promise<protocol::BlockNumber> prom;
    m_ledger->asyncGetBlockNumber(
        [&prom](Error::Ptr, protocol::BlockNumber _number) { prom.set_value(_number); });
    return prom.get_future().get();
}

protocol::BlockHeader::Ptr Executor::getLatestHeaderFromStorage()
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
