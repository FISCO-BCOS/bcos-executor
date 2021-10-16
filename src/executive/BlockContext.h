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

#include "../Common.h"
#include "bcos-framework/interfaces/executor/ExecutionMessage.h"
#include "bcos-framework/interfaces/protocol/Block.h"
#include "bcos-framework/interfaces/protocol/Transaction.h"
#include "bcos-framework/interfaces/storage/Table.h"
#include "bcos-framework/libstorage/StateStorage.h"
#include "interfaces/protocol/ProtocolTypeDef.h"
#include <tbb/concurrent_unordered_map.h>
#include <atomic>
#include <functional>
#include <memory>
#include <stack>
#include <string_view>

namespace bcos
{
namespace executor
{
class TransactionExecutive;
class PrecompiledContract;

class BlockContext : public std::enable_shared_from_this<BlockContext>
{
public:
    typedef std::shared_ptr<BlockContext> Ptr;

    BlockContext(std::shared_ptr<storage::StateStorage> storage, crypto::Hash::Ptr _hashImpl,
        bcos::protocol::BlockNumber blockNumber, h256 blockHash, uint64_t timestamp,
        int32_t blockVersion, const EVMSchedule& _schedule, bool _isWasm);

    BlockContext(std::shared_ptr<storage::StateStorage> storage, crypto::Hash::Ptr _hashImpl,
        protocol::BlockHeader::ConstPtr _current, const EVMSchedule& _schedule, bool _isWasm);

    using getTxCriticalsHandler = std::function<std::shared_ptr<std::vector<std::string>>(
        const protocol::Transaction::ConstPtr& _tx)>;
    virtual ~BlockContext(){};

    std::shared_ptr<storage::StateStorage> storage() { return m_storage; }

    uint64_t txGasLimit() const { return m_txGasLimit; }
    void setTxGasLimit(uint64_t _txGasLimit) { m_txGasLimit = _txGasLimit; }

    // Get transaction criticals, return nullptr if critical to all
    // std::shared_ptr<std::vector<std::string>> getTxCriticals(
    //     const protocol::Transaction::ConstPtr& _tx)
    // {
    //     return m_getTxCriticals(_tx);
    // }
    // void setTxCriticalsHandler(getTxCriticalsHandler _handler) { m_getTxCriticals = _handler; }
    
    auto txCriticalsHandler(const protocol::Transaction::ConstPtr& _tx)
        -> std::shared_ptr<std::vector<std::string>>;

    crypto::Hash::Ptr hashHandler() const { return m_hashImpl; }
    bool isWasm() const { return m_isWasm; }
    int64_t number() const { return m_blockNumber; }
    h256 hash() const { return m_blockHash; }
    uint64_t timestamp() const { return m_timeStamp; }
    int32_t blockVersion() const { return m_blockVersion; }
    u256 const& gasLimit() const { return m_gasLimit; }

    EVMSchedule const& evmSchedule() const { return m_schedule; }

    void insertExecutive(int64_t contextID, int64_t seq,
        std::tuple<std::shared_ptr<TransactionExecutive>,
            std::function<void(
                bcos::Error::UniquePtr&&, bcos::protocol::ExecutionMessage::UniquePtr&&)>>
            item);

    std::tuple<std::shared_ptr<TransactionExecutive>,
        std::function<void(
            bcos::Error::UniquePtr&&, bcos::protocol::ExecutionMessage::UniquePtr&&)>,
        std::function<void(bcos::Error::UniquePtr&&, CallParameters::UniquePtr)>>*
    getExecutive(int64_t contextID, int64_t seq);

    void clear() { m_executives.clear(); }

private:
    struct HashCombine
    {
        size_t operator()(const std::tuple<int64_t, int64_t>& val) const
        {
            size_t seed = hashInt64(std::get<0>(val));
            boost::hash_combine(seed, hashInt64(std::get<1>(val)));

            return seed;
        }

        std::hash<int64_t> hashInt64;
    };

    // only one request access the m_executives' value one time
    tbb::concurrent_unordered_map<std::tuple<int64_t, int64_t>,
        std::tuple<std::shared_ptr<TransactionExecutive>,
            std::function<void(bcos::Error::UniquePtr&&,
                bcos::protocol::ExecutionMessage::UniquePtr&&)>,  // for external call request
            std::function<void(bcos::Error::UniquePtr&&, CallParameters::UniquePtr)>>,  // for
                                                                                        // external
                                                                                        // call
                                                                                        // response
        HashCombine>
        m_executives;

    bcos::protocol::BlockNumber m_blockNumber;
    h256 m_blockHash;
    uint64_t m_timeStamp;
    int32_t m_blockVersion;

    EVMSchedule m_schedule;
    u256 m_gasLimit;
    bool m_isWasm = false;

    uint64_t m_txGasLimit = 300000000;
    // getTxCriticalsHandler m_getTxCriticals = nullptr;
    std::shared_ptr<storage::StateStorage> m_storage;

    // map between {receiveAddress, selector} to {ParallelConfig}
    // avoid multiple concurrent transactions of openTable to obtain
    // ParallelConfig
    // std::shared_ptr<ParallelConfigCache> m_parallelConfigCache = nullptr;
    crypto::Hash::Ptr m_hashImpl;
};

}  // namespace executor

}  // namespace bcos
