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
#include "../vm/Precompiled.h"
#include "TransactionExecutive.h"
#include "bcos-framework/interfaces/protocol/Exceptions.h"
#include "bcos-framework/interfaces/storage/StorageInterface.h"
#include "bcos-framework/interfaces/storage/Table.h"
#include "bcos-framework/libcodec/abi/ContractABICodec.h"
#include "bcos-framework/libutilities/Error.h"
#include <boost/lexical_cast.hpp>
#include <boost/throw_exception.hpp>
#include <string>

using namespace bcos::executor;
using namespace bcos::protocol;
using namespace bcos::precompiled;
using namespace std;

BlockContext::BlockContext(std::shared_ptr<storage::StateStorage> storage,
    crypto::Hash::Ptr _hashImpl, protocol::BlockHeader::ConstPtr _current,
    protocol::ExecutionMessageFactory::Ptr _executionMessageFactory, const EVMSchedule& _schedule,
    bool _isWasm)
  : m_currentHeader(std::move(_current)),
    m_executionMessageFactory(std::move(_executionMessageFactory)),
    m_schedule(_schedule),
    m_isWasm(_isWasm),
    m_storage(std::move(storage)),
    m_hashImpl(_hashImpl)
{
    // m_parallelConfigCache = make_shared<ParallelConfigCache>();
}

void BlockContext::insertExecutive(int64_t contextID, int64_t seq,
    std::tuple<std::shared_ptr<TransactionExecutive>,
        std::function<void(
            bcos::Error::UniquePtr&&, bcos::protocol::ExecutionMessage::UniquePtr&&)>>
        item)
{
    auto it = m_executives.find(std::tuple{contextID, seq});
    if (it != m_executives.end())
    {
        BOOST_THROW_EXCEPTION(
            BCOS_ERROR(-1, "Executive exists: " + boost::lexical_cast<std::string>(contextID)));
    }

    bool success;
    std::tie(it, success) = m_executives.emplace(std::tuple{contextID, seq},
        std::tuple{std::move(std::get<0>(item)), std::move(std::get<1>(item)),
            std::function<void(bcos::Error::UniquePtr&&, CallParameters::UniquePtr)>()});
}

std::tuple<std::shared_ptr<TransactionExecutive>,
    std::function<void(bcos::Error::UniquePtr&&, bcos::protocol::ExecutionMessage::UniquePtr&&)>,
    std::function<void(bcos::Error::UniquePtr&&, CallParameters::UniquePtr)>>*
BlockContext::getExecutive(int64_t contextID, int64_t seq)
{
    auto it = m_executives.find({contextID, seq});
    if (it == m_executives.end())
    {
        return nullptr;
    }

    return &(it->second);
}