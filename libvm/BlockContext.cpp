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
#include "../libstate/State.h"
#include "Precompiled.h"
#include "bcos-framework/interfaces/protocol/Exceptions.h"
#include "bcos-framework/interfaces/storage/StorageInterface.h"
#include "bcos-framework/libcodec/abi/ContractABICodec.h"
#include "bcos-framework/libtable/Table.h"

using namespace bcos::executor;
using namespace bcos::protocol;
using namespace bcos::precompiled;
using namespace std;

namespace bcos
{
BlockContext::BlockContext(std::shared_ptr<storage::TableFactoryInterface> _tableFactory,
    crypto::Hash::Ptr _hashImpl, protocol::BlockHeader::Ptr const& _current,
    const EVMSchedule& _schedule, CallBackFunction _callback, bool _isWasm)
  : m_addressCount(0x10000),
    m_currentHeader(_current),
    m_numberHash(_callback),
    m_schedule(_schedule),
    m_isWasm(_isWasm),
    m_tableFactory(_tableFactory),
    m_hashImpl(_hashImpl)
{
    m_state = make_shared<State>(m_tableFactory, m_hashImpl, _isWasm);
}

shared_ptr<PrecompiledExecResult> BlockContext::call(const string& address, bytesConstRef param,
    const string& origin, const string& sender, u256& _remainGas)
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

// TODO: check this register strategy
string BlockContext::registerPrecompiled(
    std::shared_ptr<precompiled::Precompiled> p, const std::string& _customPath)
{
    if (_customPath.empty())
    {
        auto count = ++m_addressCount;
        auto address = Address(count).hex();
        m_address2Precompiled.insert(std::make_pair(address, p));
        return address;
    }
    else
    {
        m_address2Precompiled.insert(std::make_pair(_customPath, p));
        return _customPath;
    }
}

bool BlockContext::isPrecompiled(const std::string& address) const
{
    return (m_address2Precompiled.count(address));
}

std::shared_ptr<precompiled::Precompiled> BlockContext::getPrecompiled(
    const std::string& address) const
{
    auto itPrecompiled = m_address2Precompiled.find(address);

    if (itPrecompiled != m_address2Precompiled.end())
    {
        return itPrecompiled->second;
    }
    return std::shared_ptr<precompiled::Precompiled>();
}

std::shared_ptr<executor::StateInterface> BlockContext::getState()
{
    return m_state;
}

bool BlockContext::isEthereumPrecompiled(const string& _a) const
{
    return m_precompiledContract.count(_a);
}

std::pair<bool, bytes> BlockContext::executeOriginPrecompiled(
    const string& _a, bytesConstRef _in) const
{
    return m_precompiledContract.at(_a)->execute(_in);
}

bigint BlockContext::costOfPrecompiled(const string& _a, bytesConstRef _in) const
{
    return m_precompiledContract.at(_a)->cost(_in);
}

void BlockContext::setPrecompiledContract(
    std::map<std::string, PrecompiledContract::Ptr> const& precompiledContract)
{
    m_precompiledContract = precompiledContract;
}
void BlockContext::setAddress2Precompiled(
    const string& address, std::shared_ptr<precompiled::Precompiled> precompiled)
{
    m_address2Precompiled.insert(std::make_pair(address, precompiled));
}

void BlockContext::commit()
{
    m_state->commit();
}

}  // namespace bcos
