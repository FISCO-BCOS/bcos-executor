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

#include "BlockContext.h"
#include "../precompiled/Common.h"
#include "../precompiled/Utilities.h"
#include "../vm/Precompiled.h"
#include "TransactionExecutive.h"
#include "bcos-framework/interfaces/protocol/Exceptions.h"
#include "bcos-framework/interfaces/storage/StorageInterface.h"
#include "bcos-framework/interfaces/storage/Table.h"
#include "bcos-framework/libcodec/abi/ContractABICodec.h"
#include "bcos-framework/libutilities/Error.h"
#include <boost/core/ignore_unused.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/throw_exception.hpp>
#include <string>

using namespace bcos::executor;
using namespace bcos::protocol;
using namespace bcos::precompiled;
using namespace std;

BlockContext::BlockContext(std::shared_ptr<storage::StateStorage> storage,
    crypto::Hash::Ptr _hashImpl, bcos::protocol::BlockNumber blockNumber, h256 blockHash,
    uint64_t timestamp, int32_t blockVersion, const EVMSchedule& _schedule, bool _isWasm)
  : m_blockNumber(blockNumber),
    m_blockHash(blockHash),
    m_timeStamp(timestamp),
    m_blockVersion(blockVersion),
    m_schedule(_schedule),
    m_isWasm(_isWasm),
    m_storage(std::move(storage)),
    m_hashImpl(_hashImpl)
{}

BlockContext::BlockContext(std::shared_ptr<storage::StateStorage> storage,
    crypto::Hash::Ptr _hashImpl, protocol::BlockHeader::ConstPtr _current,
    const EVMSchedule& _schedule, bool _isWasm)
  : m_blockNumber(_current->number()),
    m_blockHash(_current->hash()),
    m_timeStamp(_current->timestamp()),
    m_blockVersion(_current->version()),
    m_schedule(_schedule),
    m_isWasm(_isWasm),
    m_storage(std::move(storage)),
    m_hashImpl(_hashImpl)
{
    // m_parallelConfigCache = make_shared<ParallelConfigCache>();
}

void BlockContext::insertExecutive(int64_t contextID, int64_t seq, ExecutiveState state)
{
    auto it = m_executives.find(std::tuple{contextID, seq});
    if (it != m_executives.end())
    {
        BOOST_THROW_EXCEPTION(
            BCOS_ERROR(-1, "Executive exists: " + boost::lexical_cast<std::string>(contextID)));
    }

    bool success;
    std::tie(it, success) = m_executives.emplace(std::tuple{contextID, seq}, std::move(state));
}

bcos::executor::BlockContext::ExecutiveState* BlockContext::getExecutive(
    int64_t contextID, int64_t seq)
{
    auto it = m_executives.find({contextID, seq});
    if (it == m_executives.end())
    {
        return nullptr;
    }

    return &it->second;
}

auto BlockContext::getTxCriticals(const protocol::Transaction::ConstPtr& _tx)
    -> std::shared_ptr<std::vector<std::string>>
{
    // TODO: fix here
    boost::ignore_unused(_tx);
    return nullptr;

    /*
    if (_tx->type() == protocol::TransactionType::ContractCreation)
    {
        // Not to parallel contract creation transaction
        return nullptr;
    }
    // temp executive
    auto executive = createExecutive(shared_from_this(), std::string(_tx->to()), 0, 0);

    auto p = executive->getPrecompiled(string(_tx->to()));
    if (p)
    {
        // Precompile transaction
        if (p->isParallelPrecompiled())
        {
            auto ret = make_shared<vector<string>>(p->getParallelTag(_tx->input()));
            for (string& critical : *ret)
            {
                critical += _tx->to();
            }
            return ret;
        }
        return nullptr;
    }
    uint32_t selector = precompiled::getParamFunc(_tx->input());

    auto receiveAddress = _tx->to();
    std::shared_ptr<precompiled::ParallelConfig> config = nullptr;
    // hit the cache, fetch ParallelConfig from the cache directly
    // Note: Only when initializing DAG, get ParallelConfig, will not get
    // during transaction execution
    // TODO: add parallel config cache
    // auto parallelKey = std::make_pair(string(receiveAddress), selector);
    auto parallelConfigPrecompiled =
        std::make_shared<precompiled::ParallelConfigPrecompiled>(m_hashImpl);
    config = parallelConfigPrecompiled->getParallelConfig(
        executive, receiveAddress, selector, _tx->sender());

    if (config == nullptr)
    {
        return nullptr;
    }
    // Testing code
    auto res = make_shared<vector<string>>();

    codec::abi::ABIFunc af;
    bool isOk = af.parser(config->functionName);
    if (!isOk)
    {
        EXECUTOR_LOG(DEBUG) << LOG_DESC("[getTxCriticals] parser function signature failed, ")
                            << LOG_KV("func signature", config->functionName);

        return nullptr;
    }

    auto paramTypes = af.getParamsType();
    if (paramTypes.size() < (size_t)config->criticalSize)
    {
        EXECUTOR_LOG(DEBUG) << LOG_DESC("[getTxCriticals] params type less than  criticalSize")
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
        critical += _tx->to();
    }

    return res;
    */
}