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

#include "bcos-framework/interfaces/executor/ExecutorInterface.h"
#include "bcos-framework/interfaces/ledger/LedgerInterface.h"
#include "bcos-framework/interfaces/protocol/Block.h"
#include "bcos-framework/interfaces/protocol/BlockFactory.h"
#include "bcos-framework/interfaces/protocol/Transaction.h"
#include "bcos-framework/interfaces/protocol/TransactionReceipt.h"
#include "bcos-framework/interfaces/storage/StorageInterface.h"
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

}  // namespace protocol

namespace executor
{
class Executive;
class ExecutiveContext;
class PrecompiledContract;
class Executor : public ExecutorInterface
{
public:
    typedef std::shared_ptr<Executor> Ptr;
    typedef std::function<h256(int64_t x)> CallBackFunction;
    Executor(const protocol::BlockFactory::Ptr& _blockFactory,
        const ledger::LedgerInterface::Ptr& _ledger,
        const storage::StorageInterface::Ptr& _stateStorage, bool _isWasm);

    virtual ~Executor() {}

    std::shared_ptr<ExecutiveContext> executeBlock(
        const protocol::Block::Ptr& block, const protocol::BlockHeader::Ptr& parentBlockInfo);

    protocol::TransactionReceipt::Ptr executeTransaction(protocol::Transaction::ConstPtr _t,
        std::shared_ptr<ExecutiveContext> executiveContext,
        std::shared_ptr<executor::Executive> executive);

    void asyncGetCode(std::shared_ptr<std::string> _address,
        std::function<void(const Error::Ptr&, const std::shared_ptr<bytes>&)> _callback) override;
    void asyncExecuteTransaction(const protocol::Transaction::ConstPtr& _tx,
        std::function<void(const Error::Ptr&, const protocol::TransactionReceipt::ConstPtr&)>
            _callback) override;

    void setNumberHash(const CallBackFunction& _pNumberHash) { m_pNumberHash = _pNumberHash; }

    void stop() override { m_stop.store(true); }

private:
    std::shared_ptr<ExecutiveContext> createExecutiveContext(
        const protocol::BlockHeader::Ptr& _currentHeader);
    protocol::BlockFactory::Ptr m_blockFactory;
    ledger::LedgerInterface::Ptr m_ledger;
    storage::StorageInterface::Ptr m_stateStorage;
    CallBackFunction m_pNumberHash;
    crypto::Hash::Ptr m_hashImpl;
    unsigned int m_threadNum = -1;
    bool m_isWasm = false;
    std::atomic_bool m_stop = {false};
    std::map<std::string, std::shared_ptr<PrecompiledContract>> m_precompiledContract;
};

}  // namespace executor

}  // namespace bcos
