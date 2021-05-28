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
#pragma once

#include "../libvm/Precompiled.h"
#include "ExecutiveContextFactory.h"
#include "bcos-framework/interfaces/protocol/Block.h"
#include "bcos-framework/interfaces/protocol/Transaction.h"
#include "bcos-framework/interfaces/protocol/TransactionReceipt.h"
#include <boost/function.hpp>
#include <algorithm>
#include <functional>
#include <memory>
#include <thread>

namespace bcos
{
    DERIVE_BCOS_EXCEPTION(InvalidBlockWithBadRoot);
    DERIVE_BCOS_EXCEPTION(BlockExecutionFailed);

namespace protocol
{
class TransactionReceipt;

}  // namespace executor

namespace executor
{
class Executive;
class Executor : public std::enable_shared_from_this<Executor>
{
public:
    typedef std::shared_ptr<Executor> Ptr;
    typedef std::function<h256(int64_t x)> NumberHashCallBackFunction;
    Executor() { m_threadNum = std::max(std::thread::hardware_concurrency(), (unsigned int)1); }

    virtual ~Executor() {}

    ExecutiveContext::Ptr executeBlock(const protocol::Block::Ptr& block, BlockInfo const& parentBlockInfo);
    ExecutiveContext::Ptr parallelExecuteBlock(
        const protocol::Block::Ptr& block, BlockInfo const& parentBlockInfo);


    protocol::TransactionReceipt::Ptr executeTransaction(
        const protocol::BlockHeader::Ptr& blockHeader, protocol::Transaction::ConstPtr _t);

    protocol::TransactionReceipt::Ptr execute(protocol::Transaction::ConstPtr _t,
        executor::ExecutiveContext::Ptr executiveContext,
        std::shared_ptr<executor::Executive> executive);


    void setExecutiveContextFactory(ExecutiveContextFactory::Ptr executiveContextFactory)
    {
        m_executiveContextFactory = executiveContextFactory;
    }
    ExecutiveContextFactory::Ptr getExecutiveContextFactory() { return m_executiveContextFactory; }
    void setNumberHash(const NumberHashCallBackFunction& _pNumberHash)
    {
        m_pNumberHash = _pNumberHash;
    }

    std::shared_ptr<executor::Executive> createAndInitExecutive(
        std::shared_ptr<executor::StateInterface> _s, executor::EnvInfo const& _envInfo);
    void stop() { m_stop.store(true); }

private:
    ExecutiveContextFactory::Ptr m_executiveContextFactory;
    NumberHashCallBackFunction m_pNumberHash;
    unsigned int m_threadNum = -1;

    std::mutex m_executingMutex;
    std::atomic<int64_t> m_executingNumber = {0};
    std::atomic_bool m_stop = {false};
};

}  // namespace executor

}  // namespace bcos
