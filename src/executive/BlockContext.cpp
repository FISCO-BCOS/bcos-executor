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
  : m_addressCount(0x10000),
    m_currentHeader(std::move(_current)),
    m_executionMessageFactory(std::move(_executionMessageFactory)),
    m_schedule(_schedule),
    m_isWasm(_isWasm),
    m_storage(std::move(storage)),
    m_hashImpl(_hashImpl)
{
    // m_parallelConfigCache = make_shared<ParallelConfigCache>();
}

shared_ptr<PrecompiledExecResult> BlockContext::call(const string& address, bytesConstRef param,
    const string& origin, const string& sender, int64_t& _remainGas)
{
    try
    {
        auto p = getPrecompiled(address);

        if (p)
        {
            auto execResult = p->call(shared_from_this(), param, origin, sender, _remainGas);
            return execResult;
        }
        else
        {
            EXECUTIVE_LOG(DEBUG) << LOG_DESC("[call]Can't find address")
                                 << LOG_KV("address", address);
            return nullptr;
        }
    }
    catch (protocol::PrecompiledError& e)
    {
        EXECUTIVE_LOG(ERROR) << "PrecompiledError" << LOG_KV("address", address)
                             << LOG_KV("message:", e.what());
        BOOST_THROW_EXCEPTION(e);
    }
    catch (std::exception& e)
    {
        EXECUTIVE_LOG(ERROR) << LOG_DESC("[call]Precompiled call error")
                             << LOG_KV("EINFO", boost::diagnostic_information(e));

        throw PrecompiledError();
    }
}

string BlockContext::registerPrecompiled(std::shared_ptr<precompiled::Precompiled> p)
{
    auto count = ++m_addressCount;
    std::stringstream stream;
    stream << std::setfill('0') << std::setw(40) << std::hex << count;
    auto address = stream.str();
    m_address2Precompiled.insert(std::make_pair(address, p));
    return address;
}

bool BlockContext::isPrecompiled(const std::string& address) const
{
    return (m_address2Precompiled.count(address));
}

std::shared_ptr<Precompiled> BlockContext::getPrecompiled(const std::string& address) const
{
    auto itPrecompiled = m_address2Precompiled.find(address);

    if (itPrecompiled != m_address2Precompiled.end())
    {
        return itPrecompiled->second;
    }
    return std::shared_ptr<precompiled::Precompiled>();
}

bool BlockContext::isEthereumPrecompiled(const string& _a) const
{
    return m_precompiledContract->find(_a) != m_precompiledContract->end();
}

std::pair<bool, bcos::bytes> BlockContext::executeOriginPrecompiled(
    const string& _a, bytesConstRef _in) const
{
    return m_precompiledContract->at(_a)->execute(_in);
}

int64_t BlockContext::costOfPrecompiled(const string& _a, bytesConstRef _in) const
{
    return m_precompiledContract->at(_a)->cost(_in).convert_to<int64_t>();
}

void BlockContext::setPrecompiledContract(
    std::shared_ptr<const std::map<std::string, PrecompiledContract::Ptr>> precompiledContract)
{
    m_precompiledContract = std::move(precompiledContract);
}
void BlockContext::setAddress2Precompiled(
    const string& address, std::shared_ptr<precompiled::Precompiled> precompiled)
{
    m_address2Precompiled.insert(std::make_pair(address, precompiled));
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
    std::tie(it, success) = m_executives.emplace(std::tuple{contextID, seq}, std::move(item));
}

std::tuple<std::shared_ptr<TransactionExecutive>,
    std::function<void(bcos::Error::UniquePtr&&, bcos::protocol::ExecutionMessage::UniquePtr&&)>>*
BlockContext::getExecutive(int64_t contextID, int64_t seq)
{
    auto it = m_executives.find({contextID, seq});
    if (it == m_executives.end())
    {
        return nullptr;
    }

    return &(it->second);
}