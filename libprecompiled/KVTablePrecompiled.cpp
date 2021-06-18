/**
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
 * @file KVTablePrecompiled.cpp
 * @author: kyonRay
 * @date 2021-05-27
 */

#include "KVTablePrecompiled.h"
#include "Common.h"
#include "EntryPrecompiled.h"
#include "PrecompiledResult.h"
#include "Utilities.h"
#include <bcos-framework/interfaces/protocol/CommonError.h>
#include <bcos-framework/interfaces/protocol/Exceptions.h>
#include <bcos-framework/interfaces/storage/TableInterface.h>

using namespace bcos;
using namespace bcos::storage;
using namespace bcos::precompiled;
using namespace bcos::protocol;
using namespace bcos::executor;

const char* const KV_TABLE_METHOD_GET = "get(string)";
const char* const KV_TABLE_METHOD_SET = "set(string,address)";
const char* const KV_TABLE_METHOD_NEW_ENTRY = "newEntry()";


KVTablePrecompiled::KVTablePrecompiled(crypto::Hash::Ptr _hashImpl) : Precompiled(_hashImpl)
{
    name2Selector[KV_TABLE_METHOD_GET] = getFuncSelector(KV_TABLE_METHOD_GET, _hashImpl);
    name2Selector[KV_TABLE_METHOD_SET] = getFuncSelector(KV_TABLE_METHOD_SET, _hashImpl);
    name2Selector[KV_TABLE_METHOD_NEW_ENTRY] =
        getFuncSelector(KV_TABLE_METHOD_NEW_ENTRY, _hashImpl);
}

std::string KVTablePrecompiled::toString()
{
    return "KVTable";
}

PrecompiledExecResult::Ptr KVTablePrecompiled::call(
    std::shared_ptr<executor::BlockContext> _context, bytesConstRef _param,
    const std::string& _origin, const std::string& _sender, u256& _remainGas)
{
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("KVTable") << LOG_DESC("call") << LOG_KV("func", func);

    m_codec = std::make_shared<PrecompiledCodec>(_context->hashHandler(), _context->isWasm());
    auto callResult = std::make_shared<PrecompiledExecResult>();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();

    gasPricer->setMemUsed(_param.size());

    if (func == name2Selector[KV_TABLE_METHOD_GET])
    {  // get(string)
        std::string key;
        m_codec->decode(data, key);
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("KVTable") << LOG_KV("get", key);

        auto entry = m_table->getRow(key);
        gasPricer->updateMemUsed(entry->capacityOfHashField());
        gasPricer->appendOperation(InterfaceOpcode::Select, 1);

        if (!entry)
        {
            callResult->setExecResult(m_codec->encode(false));
        }
        else
        {
            auto entryPrecompiled = std::make_shared<EntryPrecompiled>(m_hashImpl);
            // CachedStorage return entry use copy from
            entryPrecompiled->setEntry(entry);
            if (_context->isWasm())
            {
                std::string newAddress = _context->registerPrecompiled(entryPrecompiled);
                callResult->setExecResult(m_codec->encode(true, newAddress));
            }
            else
            {
                Address newAddress = Address(_context->registerPrecompiled(entryPrecompiled));
                callResult->setExecResult(m_codec->encode(true, newAddress));
            }
            auto newAddress = Address(_context->registerPrecompiled(entryPrecompiled));
            callResult->setExecResult(m_codec->encode(true, newAddress));
        }
    }
    else if (func == name2Selector[KV_TABLE_METHOD_SET])
    {  // set(string,address)
        if (!checkAuthority(_context->getTableFactory(), _origin, _sender))
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("TablePrecompiled") << LOG_DESC("permission denied")
                                   << LOG_KV("origin", _origin) << LOG_KV("contract", _sender);
            BOOST_THROW_EXCEPTION(
                PrecompiledError() << errinfo_comment(
                    "Permission denied. " + _origin + " can't call contract " + _sender));
        }
        std::string key;
        Address entryAddress;
        m_codec->decode(data, key, entryAddress);
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("KVTable") << LOG_KV("set", key);
        EntryPrecompiled::Ptr entryPrecompiled = std::dynamic_pointer_cast<EntryPrecompiled>(
            _context->getPrecompiled(std::string((char*)entryAddress.data(), entryAddress.size)));
        auto entry = entryPrecompiled->getEntry();
        checkLengthValidate(
            key, USER_TABLE_KEY_VALUE_MAX_LENGTH, CODE_TABLE_KEY_VALUE_LENGTH_OVERFLOW);

        auto it = entry->begin();
        for (; it != entry->end(); ++it)
        {
            checkLengthValidate(it->second, USER_TABLE_FIELD_VALUE_MAX_LENGTH,
                CODE_TABLE_KEY_VALUE_LENGTH_OVERFLOW);
        }

        m_table->setRow(key, entry);
        auto commitResult = _context->getTableFactory()->commit();
        if (!commitResult.second || commitResult.second->errorCode() == CommonError::SUCCESS)
        {
            if (commitResult.first > 0)
            {
                gasPricer->setMemUsed(entry->capacityOfHashField() * commitResult.first);
                gasPricer->appendOperation(InterfaceOpcode::Insert, commitResult.first);
            }
        }
        callResult->setExecResult(m_codec->encode(s256(commitResult.first)));
    }
    else if (func == name2Selector[KV_TABLE_METHOD_NEW_ENTRY])
    {  // newEntry()
        auto entry = m_table->newEntry();
        auto entryPrecompiled = std::make_shared<EntryPrecompiled>(m_hashImpl);
        entryPrecompiled->setEntry(entry);

        auto newAddress = Address(_context->registerPrecompiled(entryPrecompiled));
        callResult->setExecResult(m_codec->encode(newAddress));
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("KVTablePrecompiled")
                               << LOG_DESC("call undefined function!");
    }
    gasPricer->updateMemUsed(callResult->m_execResult.size());
    _remainGas -= gasPricer->calTotalGas();
    return callResult;
}

crypto::HashType KVTablePrecompiled::hash()
{
    return m_table->hash();
}