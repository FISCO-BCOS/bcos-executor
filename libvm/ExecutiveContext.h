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
 * @date: 2021-05-26
 */

#pragma once

#include "../libstate/StateInterface.h"
#include "Common.h"
#include "PrecompiledContract.h"
#include "bcos-framework/interfaces/protocol/Block.h"
#include "bcos-framework/interfaces/protocol/Transaction.h"
#include "bcos-framework/interfaces/storage/TableInterface.h"

// #include "bcos-framework/libtable/Table.h"
// for concurrent_map
#define TBB_PREVIEW_CONCURRENT_ORDERED_CONTAINERS 1
#include <tbb/concurrent_map.h>
#include <tbb/concurrent_unordered_map.h>
#include <atomic>
#include <functional>
#include <memory>

namespace bcos
{
namespace storage
{
class TableFactoryInterface;
}  // namespace storage

namespace executor
{
class StateInterface;
}
namespace precompiled
{
class Precompiled;
struct ParallelConfig;
struct PrecompiledExecResult;
}  // namespace precompiled
namespace executor
{
struct BlockInfo
{
    crypto::HashType hash;
    protocol::BlockNumber number;
    crypto::HashType stateRoot;
};
class ExecutiveContext : public std::enable_shared_from_this<ExecutiveContext>
{
public:
    typedef std::shared_ptr<ExecutiveContext> Ptr;
    ExecutiveContext(
        std::shared_ptr<storage::TableFactoryInterface> _tableFactory, crypto::Hash::Ptr _hashImpl)
      : m_addressCount(0x10000), m_tableFactory(_tableFactory), m_hashImpl(_hashImpl)
    {}
    using getTxCriticalsHandler = std::function<std::shared_ptr<std::vector<std::string>>(
        const protocol::Transaction::ConstPtr& _tx)>;
    virtual ~ExecutiveContext()
    {
        if (m_tableFactory)
        {
            m_tableFactory->commit();
        }
    };

    virtual std::shared_ptr<precompiled::PrecompiledExecResult> call(
        const std::string& address, bytesConstRef param, const std::string& origin,
        const std::string& sender, u256& _remainGas);

    virtual std::string registerPrecompiled(std::shared_ptr<precompiled::Precompiled> p);

    virtual bool isPrecompiled(const std::string& _address) const;

    std::shared_ptr<precompiled::Precompiled> getPrecompiled(
        const std::string& _address) const;

    void setAddress2Precompiled(
        const std::string& _address, std::shared_ptr<precompiled::Precompiled> precompiled);

    BlockInfo const& blockInfo() { return m_blockInfo; }
    void setBlockInfo(BlockInfo blockInfo) { m_blockInfo = blockInfo; }

    std::shared_ptr<executor::StateInterface> getState();
    void setState(std::shared_ptr<executor::StateInterface> state);

    virtual bool isEthereumPrecompiled(const std::string& _a) const;

    virtual std::pair<bool, bytes> executeOriginPrecompiled(
        const std::string& _a, bytesConstRef _in) const;

    virtual bigint costOfPrecompiled(const std::string& _a, bytesConstRef _in) const;

    void setPrecompiledContract(
        std::unordered_map<std::string, PrecompiledContract> const& precompiledContract);

    void commit();

    void setMemoryTableFactory(std::shared_ptr<storage::TableFactoryInterface> tableFactory)
    {
        m_tableFactory = tableFactory;
    }

    std::shared_ptr<storage::TableFactoryInterface> getTableFactory() { return m_tableFactory; }

    uint64_t txGasLimit() const { return m_txGasLimit; }
    void setTxGasLimit(uint64_t _txGasLimit) { m_txGasLimit = _txGasLimit; }

    // Get transaction criticals, return nullptr if critical to all
    std::shared_ptr<std::vector<std::string>> getTxCriticals(const protocol::Transaction::ConstPtr& _tx)
    {
        return m_getTxCriticals(_tx);
    }
    void setTxCriticalsHandler(getTxCriticalsHandler _handler) { m_getTxCriticals = _handler; }
    crypto::Hash::Ptr hashHandler() const { return m_hashImpl; }
    bool isWasm() const{ return m_isWasm; }

private:
    tbb::concurrent_unordered_map<std::string, std::shared_ptr<precompiled::Precompiled>,
        std::hash<std::string>>
        m_address2Precompiled;
    std::atomic<int> m_addressCount;
    BlockInfo m_blockInfo;
    bool m_isWasm = false;

    std::shared_ptr<executor::StateInterface> m_stateFace;
    std::unordered_map<std::string, PrecompiledContract> m_precompiledContract;
    uint64_t m_txGasLimit = 300000000;
    getTxCriticalsHandler m_getTxCriticals = nullptr;
    std::shared_ptr<storage::TableFactoryInterface> m_tableFactory;
    crypto::Hash::Ptr m_hashImpl;
};

/// the information related to the EVM
class EnvInfo
{
public:
    typedef std::function<crypto::HashType(int64_t x)> CallBackFunction;

    // Constructor with custom gasLimit - used in some synthetic scenarios like eth_estimateGas RPC
    // method
    EnvInfo(protocol::BlockHeader::Ptr const& _current, CallBackFunction _callback,
        u256 const& _gasLimit)
      : EnvInfo(_current, _callback)
    {
        m_gasLimit = _gasLimit;
    }

    EnvInfo(protocol::BlockHeader::Ptr const& _current, CallBackFunction _callback)
      : m_headerInfo(_current), m_numberHash(_callback)
    {}

    /// @return block header
    protocol::BlockHeader::Ptr const& header() const { return m_headerInfo; }

    /// @return block number
    int64_t number() const { return m_headerInfo->number(); }

    /// @return timestamp
    uint64_t timestamp() const { return m_headerInfo->timestamp(); }

    /// @return gasLimit of the block header
    u256 const& gasLimit() const { return m_gasLimit; }

    crypto::HashType numberHash(int64_t x) const { return m_numberHash(x); }

    EVMSchedule const& evmSchedule() const { return *m_schedule; }

    std::shared_ptr<executor::ExecutiveContext> Context() const { return m_executiveContext; }

    void setContext(std::shared_ptr<executor::ExecutiveContext> _executiveContext)
    {
        m_executiveContext = _executiveContext;
    }
    bool isSMCrypto() const { return useSMCrypto; }
    crypto::Hash::Ptr hashHandler() const { return m_executiveContext->hashHandler(); }

private:
    protocol::BlockHeader::Ptr m_headerInfo;
    CallBackFunction m_numberHash;
    std::shared_ptr<const EVMSchedule> m_schedule = nullptr;
    u256 m_gasLimit;
    std::shared_ptr<executor::ExecutiveContext> m_executiveContext;
    bool useSMCrypto = false;
};
}  // namespace executor

}  // namespace bcos
