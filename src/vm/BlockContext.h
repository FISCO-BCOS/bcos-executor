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
 * @date: 2021-05-26
 */

#pragma once

// for concurrent_map
#define TBB_PREVIEW_CONCURRENT_ORDERED_CONTAINERS 1

#include "../state/StateInterface.h"
#include "Common.h"
#include "bcos-framework/interfaces/executor/ExecutionResult.h"
#include "bcos-framework/interfaces/protocol/Block.h"
#include "bcos-framework/interfaces/protocol/Transaction.h"
#include "bcos-framework/interfaces/storage/Table.h"
#include <tbb/concurrent_map.h>
#include <tbb/concurrent_unordered_map.h>
#include <atomic>
#include <functional>
#include <memory>
#include <stack>

namespace bcos
{
namespace storage
{
class StateStorage;
}  // namespace storage

namespace precompiled
{
class Precompiled;
struct ParallelConfig;
struct PrecompiledExecResult;
}  // namespace precompiled
namespace executor
{
class StateInterface;
class TransactionExecutive;
class PrecompiledContract;
typedef std::function<crypto::HashType(int64_t x)> CallBackFunction;
class BlockContext : public std::enable_shared_from_this<BlockContext>
{
public:
    typedef std::shared_ptr<BlockContext> Ptr;
    using ParallelConfigCache = tbb::concurrent_map<std::pair<std::string, uint32_t>,
        std::shared_ptr<bcos::precompiled::ParallelConfig>>;
    BlockContext(std::shared_ptr<storage::StateStorage> _tableFactory, crypto::Hash::Ptr _hashImpl,
        const protocol::BlockHeader::ConstPtr& _current,
        protocol::ExecutionResultFactory::Ptr _executionResultFactory, const EVMSchedule& _schedule,
        CallBackFunction _callback, bool _isWasm);
    using getTxCriticalsHandler = std::function<std::shared_ptr<std::vector<std::string>>(
        const protocol::Transaction::ConstPtr& _tx)>;
    virtual ~BlockContext(){};

    virtual std::shared_ptr<precompiled::PrecompiledExecResult> call(const std::string& address,
        bytesConstRef param, const std::string& origin, const std::string& sender,
        u256& _remainGas);

    virtual std::string registerPrecompiled(std::shared_ptr<precompiled::Precompiled> p);

    virtual bool isPrecompiled(const std::string& _address) const;

    std::shared_ptr<precompiled::Precompiled> getPrecompiled(const std::string& _address) const;

    void setAddress2Precompiled(
        const std::string& _address, std::shared_ptr<precompiled::Precompiled> precompiled);

    std::shared_ptr<executor::StateInterface> getState();

    virtual bool isEthereumPrecompiled(const std::string& _a) const;

    virtual std::pair<bool, bytes> executeOriginPrecompiled(
        const std::string& _a, bytesConstRef _in) const;

    virtual bigint costOfPrecompiled(const std::string& _a, bytesConstRef _in) const;

    virtual std::shared_ptr<ParallelConfigCache> getParallelConfigCache()
    {
        return m_parallelConfigCache;
    }

    void setPrecompiledContract(
        std::map<std::string, std::shared_ptr<PrecompiledContract>> const& precompiledContract);

    void commit();

    std::shared_ptr<storage::StateStorage> getTableFactory() { return m_tableFactory; }

    uint64_t txGasLimit() const { return m_txGasLimit; }
    void setTxGasLimit(uint64_t _txGasLimit) { m_txGasLimit = _txGasLimit; }

    // Get transaction criticals, return nullptr if critical to all
    std::shared_ptr<std::vector<std::string>> getTxCriticals(
        const protocol::Transaction::ConstPtr& _tx)
    {
        return m_getTxCriticals(_tx);
    }
    void setTxCriticalsHandler(getTxCriticalsHandler _handler) { m_getTxCriticals = _handler; }
    crypto::Hash::Ptr hashHandler() const { return m_hashImpl; }
    bool isWasm() const { return m_isWasm; }
    /// @return block number
    int64_t currentNumber() const { return m_currentHeader->number(); }

    /// @return timestamp
    uint64_t timestamp() const
    {  // FIXME: update framework when timestamp() of blockheader is const
        auto header = const_cast<protocol::BlockHeader*>(m_currentHeader.get());
        return header->timestamp();
    }
    int32_t blockVersion() const { return m_currentHeader->version(); }
    /// @return gasLimit of the block header
    u256 const& gasLimit() const { return m_gasLimit; }
    protocol::BlockHeader::ConstPtr currentBlockHeader() { return m_currentHeader; }
    crypto::HashType numberHash(int64_t x) const { return m_numberHash(x); }

    EVMSchedule const& evmSchedule() const { return m_schedule; }
    void insertExecutive(
        int64_t contextID, std::string_view address, std::shared_ptr<TransactionExecutive>);
    std::shared_ptr<TransactionExecutive> getLastExecutiveOf(
        int64_t contextID, std::string_view address);

    protocol::ExecutionResult::Ptr createExecutionResult(int64_t _contextID, CallParameters& _p);
    protocol::ExecutionResult::Ptr createExecutionResult(int64_t _contextID, u256& _gasLeft, bytesConstRef _code, std::optional<u256> _salt);

    void clear() { m_executives.clear(); }

private:
    tbb::concurrent_unordered_map<std::string, std::shared_ptr<precompiled::Precompiled>,
        std::hash<std::string>>
        m_address2Precompiled;
    // only one request access the m_executives' value one time
    tbb::concurrent_unordered_map<int64_t,
        std::map<std::string, std::stack<std::shared_ptr<TransactionExecutive>>>>
        m_executives;
    std::atomic<int> m_addressCount;
    protocol::BlockHeader::ConstPtr m_currentHeader;
    protocol::ExecutionResultFactory::Ptr m_executionResultFactory;
    CallBackFunction m_numberHash;
    EVMSchedule m_schedule;
    u256 m_gasLimit;
    bool m_isWasm = false;
    std::shared_ptr<executor::StateInterface> m_state;
    std::map<std::string, std::shared_ptr<PrecompiledContract>> m_precompiledContract;
    uint64_t m_txGasLimit = 300000000;
    getTxCriticalsHandler m_getTxCriticals = nullptr;
    std::shared_ptr<storage::StateStorage> m_tableFactory;
    // map between {receiveAddress, selector} to {ParallelConfig}
    // avoid multiple concurrent transactions of openTable to obtain ParallelConfig
    std::shared_ptr<ParallelConfigCache> m_parallelConfigCache = nullptr;
    crypto::Hash::Ptr m_hashImpl;
};

}  // namespace executor

}  // namespace bcos
