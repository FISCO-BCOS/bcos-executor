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
class Table;
class Storage;
}  // namespace storage

namespace executor
{
class StateInterface;
}
namespace precompiled
{
class Precompiled;
class PrecompiledExecResultFactory;
class ParallelConfigPrecompiled;
struct ParallelConfig;
struct PrecompiledExecResult;
}  // namespace precompiled
namespace executor
{
class ExecutiveContext : public std::enable_shared_from_this<ExecutiveContext>
{
public:
    typedef std::shared_ptr<ExecutiveContext> Ptr;
    using ParallelConfigKey = std::pair<Address, uint32_t>;
    ExecutiveContext() : m_addressCount(0x10000) {}
    virtual ~ExecutiveContext()
    {
        if (m_tableFactory)
        {
            m_tableFactory->commit();
        }
    };

    virtual std::shared_ptr<precompiled::PrecompiledExecResult> call(
        const std::string_view& address, bytesConstRef param, const std::string_view& origin,
        const std::string_view& sender);

    virtual Address registerPrecompiled(std::shared_ptr<precompiled::Precompiled> p);

    virtual bool isPrecompiled(const std::string_view& _address) const;

    std::shared_ptr<precompiled::Precompiled> getPrecompiled(const std::string_view& _address) const;

    void setAddress2Precompiled(
        const std::string_view& _address, std::shared_ptr<precompiled::Precompiled> precompiled);

    void registerParallelPrecompiled(std::shared_ptr<precompiled::Precompiled> _precompiled);

    void setPrecompiledExecResultFactory(
        std::shared_ptr<precompiled::PrecompiledExecResultFactory> _precompiledExecResultFactory);

    BlockInfo const& blockInfo() { return m_blockInfo; }
    void setBlockInfo(BlockInfo blockInfo) { m_blockInfo = blockInfo; }

    std::shared_ptr<executor::StateInterface> getState();
    void setState(std::shared_ptr<executor::StateInterface> state);

    virtual bool isEthereumPrecompiled(const std::string_view& _a) const;

    virtual std::pair<bool, bytes> executeOriginPrecompiled(
        const std::string_view& _a, bytesConstRef _in) const;

    virtual bigint costOfPrecompiled(const std::string_view& _a, bytesConstRef _in) const;

    void setPrecompiledContract(
        std::unordered_map<std::string, PrecompiledContract> const& precompiledContract);

    void dbCommit(protocol::Block& block);

    void setMemoryTableFactory(std::shared_ptr<storage::TableFactoryInterface> memoryTableFactory)
    {
        m_tableFactory = memoryTableFactory;
    }

    std::shared_ptr<storage::TableFactoryInterface> getTableFactory() { return m_tableFactory; }

    uint64_t txGasLimit() const { return m_txGasLimit; }
    void setTxGasLimit(uint64_t _txGasLimit) { m_txGasLimit = _txGasLimit; }

    // Get transaction criticals, return nullptr if critical to all
    std::shared_ptr<std::vector<std::string>> getTxCriticals(const protocol::Transaction& _tx);

    std::shared_ptr<storage::Storage> stateStorage();
    void setStateStorage(std::shared_ptr<storage::Storage> _stateStorage);

private:
    tbb::concurrent_unordered_map<std::string, std::shared_ptr<precompiled::Precompiled>,
        std::hash<std::string>>
        m_address2Precompiled;
    std::atomic<int> m_addressCount;
    BlockInfo m_blockInfo;
    std::shared_ptr<executor::StateInterface> m_stateFace;
    std::unordered_map<std::string, PrecompiledContract> m_precompiledContract;
    std::shared_ptr<storage::TableFactoryInterface> m_tableFactory;
    uint64_t m_txGasLimit = 300000000;

    std::shared_ptr<precompiled::PrecompiledExecResultFactory> m_precompiledExecResultFactory;
    std::shared_ptr<precompiled::ParallelConfigPrecompiled> m_parallelConfigPrecompiled;

    // map between {receiveAddress, selector} to {ParallelConfig}
    // avoid multiple concurrent transactions of openTable to obtain ParallelConfig
    tbb::concurrent_map<ParallelConfigKey, std::shared_ptr<precompiled::ParallelConfig>>
        m_parallelConfigCache;
    std::shared_ptr<storage::Storage> m_stateStorage;
};

}  // namespace executor

}  // namespace bcos
