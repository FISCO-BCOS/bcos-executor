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
    m_hashImpl(_hashImpl),
    m_addressCount(0x10000)
{}

BlockContext::BlockContext(std::shared_ptr<storage::StateStorage> storage,
    crypto::Hash::Ptr _hashImpl, protocol::BlockHeader::ConstPtr _current,
    const EVMSchedule& _schedule, bool _isWasm)
  : BlockContext(storage, _hashImpl, _current->number(), _current->hash(), _current->timestamp(),
        _current->version(), _schedule, _isWasm)
{}

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
string BlockContext::registerPrecompiled(std::shared_ptr<precompiled::Precompiled> p)
{
    auto count = ++m_addressCount;
    std::stringstream stream;
    stream << std::setfill('0') << std::setw(40) << std::hex << count;
    auto address = stream.str();
    m_dynamicPrecompiled.insert(std::make_pair(address, p));
    return address;
}

bool BlockContext::isDynamicPrecompiled(const std::string& address) const
{
    return m_dynamicPrecompiled.count(address) > 0;
}

std::shared_ptr<Precompiled> BlockContext::getDynamicPrecompiled(const std::string& address) const
{
    auto dynamicPrecompiled = m_dynamicPrecompiled.find(address);
    return (dynamicPrecompiled != m_dynamicPrecompiled.end()) ?
               dynamicPrecompiled->second :
               std::shared_ptr<precompiled::Precompiled>();
}
