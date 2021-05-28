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
#include "../libvm/Executive.h"
#include "../libvm/ExecutiveContext.h"
#include "TxDAG.h"
#include "bcos-framework/interfaces/protocol/TransactionReceipt.h"
#include "bcos-framework/libtable/Table.h"
#include <tbb/parallel_for.h>
#include <exception>
#include <thread>

using namespace bcos;
using namespace std;
using namespace bcos::executor;
using namespace bcos::protocol;
using namespace bcos::storage;

ExecutiveContext::Ptr Executor::executeBlock(
    const protocol::Block::Ptr& block, BlockInfo const& parentBlockInfo)
{
    // return nullptr prepare to exit when m_stop is true
    if (m_stop.load())
    {
        return nullptr;
    }
    if (block->blockHeader()->number() < m_executingNumber)
    {
        return nullptr;
    }
    std::lock_guard<std::mutex> l(m_executingMutex);
    if (block->blockHeader()->number() < m_executingNumber)
    {
        return nullptr;
    }
    ExecutiveContext::Ptr context = nullptr;
    try
    {
        context = parallelExecuteBlock(block, parentBlockInfo);
    }
    catch (exception& e)
    {
        EXECUTOR_LOG(ERROR) << LOG_BADGE("executeBlock") << LOG_DESC("executeBlock exception")
                            << LOG_KV("blockNumber", block->blockHeader()->number());
        return nullptr;
    }
    m_executingNumber = block->blockHeader()->number();
    return context;
}

ExecutiveContext::Ptr Executor::parallelExecuteBlock(
    const protocol::Block::Ptr& block, BlockInfo const& parentBlockInfo)

{
    EXECUTOR_LOG(INFO) << LOG_DESC("[executeBlock]Executing block")
                       << LOG_KV("txNum", block->transactionsSize())
                       << LOG_KV("num", block->blockHeader()->number())
                       << LOG_KV("parentHash", parentBlockInfo.hash)
                       << LOG_KV("parentNum", parentBlockInfo.number)
                       << LOG_KV("parentStateRoot", parentBlockInfo.stateRoot);

    auto start_time = utcTime();
    auto record_time = utcTime();
    ExecutiveContext::Ptr executiveContext =
        m_executiveContextFactory->createExecutiveContext(parentBlockInfo);

    auto initExeCtx_time_cost = utcTime() - record_time;
    record_time = utcTime();

    auto tmpHeader = block->blockHeader();
    // FIXME: check logic below
    // block->clearAllReceipts();
    // block->resizeTransactionReceipt(block->transactionsSize());
    auto perpareBlock_time_cost = utcTime() - record_time;
    record_time = utcTime();

    shared_ptr<TxDAG> txDag = make_shared<TxDAG>();
    txDag->init(executiveContext, block);

    txDag->setTxExecuteFunc([&](Transaction::ConstPtr _tr, ID _txId, Executive::Ptr _executive) {
        auto resultReceipt = execute(_tr, executiveContext, _executive);

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
                EnvInfo envInfo(block->blockHeader(), m_pNumberHash, 0);
                envInfo.setContext(executiveContext);
                auto executive = createAndInitExecutive(executiveContext->getState(), envInfo);

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
    if (tmpHeader->receiptRoot() != h256())
    {
        if (tmpHeader != block->blockHeader())
        {
            EXECUTOR_LOG(ERROR) << "Invalid Block with bad stateRoot or receiptRoot"
                                << LOG_KV("blkNum", block->blockHeader()->number())
                                << LOG_KV("originHash", tmpHeader->hash().abridged())
                                << LOG_KV("curHash", block->blockHeader()->hash().abridged())
                                << LOG_KV("orgReceipt", tmpHeader->receiptRoot().abridged())
                                << LOG_KV(
                                       "curRecepit", block->blockHeader()->receiptRoot().abridged())
                                << LOG_KV("orgTxRoot", tmpHeader->txsRoot().abridged())
                                << LOG_KV("curTxRoot", block->blockHeader()->txsRoot().abridged())
                                << LOG_KV("orgState", tmpHeader->stateRoot().abridged())
                                << LOG_KV("curState", block->blockHeader()->stateRoot().abridged());
#ifdef FISCO_DEBUG
            auto receipts = block->transactionReceipts();
            for (size_t i = 0; i < receipts->size(); ++i)
            {
                EXECUTOR_LOG(ERROR) << LOG_BADGE("FISCO_DEBUG") << LOG_KV("index", i)
                                    << LOG_KV("hash", block->transaction(i)->hash())
                                    << ",receipt=" << *receipts->at(i);
            }
#endif
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


TransactionReceipt::Ptr Executor::executeTransaction(
    const protocol::BlockHeader::Ptr& blockHeader, protocol::Transaction::ConstPtr _t)
{
    BlockInfo blockInfo{blockHeader->hash(), blockHeader->number(), blockHeader->stateRoot()};
    ExecutiveContext::Ptr executiveContext =
        m_executiveContextFactory->createExecutiveContext(blockInfo);

    EnvInfo envInfo(blockHeader, m_pNumberHash, 0);
    envInfo.setContext(executiveContext);
    auto executive = createAndInitExecutive(executiveContext->getState(), envInfo);
    // only Rpc::call will use executeTransaction, RPC do catch exception
    return execute(_t, executiveContext, executive);
}

protocol::TransactionReceipt::Ptr Executor::execute(protocol::Transaction::ConstPtr _t,
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

executor::Executive::Ptr Executor::createAndInitExecutive(
    std::shared_ptr<StateInterface> _s, executor::EnvInfo const& _envInfo)
{
    return std::make_shared<Executive>(_s, _envInfo);
}
