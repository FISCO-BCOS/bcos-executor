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
 * @file TablePrecompiled.cpp
 * @author: kyonRay
 * @date 2021-05-30
 */

#include "TablePrecompiled.h"
#include "Common.h"
#include "ConditionPrecompiled.h"
#include "EntriesPrecompiled.h"
#include "EntryPrecompiled.h"
#include "PrecompiledResult.h"
#include <bcos-framework/interfaces/protocol/Exceptions.h>

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::precompiled;
using namespace bcos::protocol;

// TODO: address?
const char* const TABLE_METHOD_SLT_STR_ADD = "select(address)";
const char* const TABLE_METHOD_INS_STR_ADD = "insert(address)";
const char* const TABLE_METHOD_NEW_COND = "newCondition()";
const char* const TABLE_METHOD_NEW_ENTRY = "newEntry()";
const char* const TABLE_METHOD_RE_STR_ADD = "remove(address)";
const char* const TABLE_METHOD_UP_STR_2ADD = "update(address,address)";


TablePrecompiled::TablePrecompiled(crypto::Hash::Ptr _hashImpl) : Precompiled(_hashImpl)
{
    name2Selector[TABLE_METHOD_SLT_STR_ADD] = getFuncSelector(TABLE_METHOD_SLT_STR_ADD, _hashImpl);
    name2Selector[TABLE_METHOD_INS_STR_ADD] = getFuncSelector(TABLE_METHOD_INS_STR_ADD, _hashImpl);
    name2Selector[TABLE_METHOD_NEW_COND] = getFuncSelector(TABLE_METHOD_NEW_COND, _hashImpl);
    name2Selector[TABLE_METHOD_NEW_ENTRY] = getFuncSelector(TABLE_METHOD_NEW_ENTRY, _hashImpl);
    name2Selector[TABLE_METHOD_RE_STR_ADD] = getFuncSelector(TABLE_METHOD_RE_STR_ADD, _hashImpl);
    name2Selector[TABLE_METHOD_UP_STR_2ADD] = getFuncSelector(TABLE_METHOD_UP_STR_2ADD, _hashImpl);
}

std::string TablePrecompiled::toString()
{
    return "Table";
}

crypto::HashType TablePrecompiled::hash()
{
    return m_table->hash();
}

PrecompiledExecResult::Ptr TablePrecompiled::call(
    std::shared_ptr<executor::BlockContext> _context, bytesConstRef _param,
    const std::string& _origin, const std::string& _sender, u256& _remainGas)
{
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);

    m_codec = std::make_shared<PrecompiledCodec>(nullptr, _context->isWasm());
    auto callResult = std::make_shared<PrecompiledExecResult>();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();
    gasPricer->setMemUsed(_param.size());

    if (func == name2Selector[TABLE_METHOD_SLT_STR_ADD])
    {
        // select(address)
        ConditionPrecompiled::Ptr conditionPrecompiled = nullptr;
        if (_context->isWasm())
        {
            std::string conditionAddress;
            m_codec->decode(data, conditionAddress);
            conditionPrecompiled = std::dynamic_pointer_cast<ConditionPrecompiled>(
                _context->getPrecompiled(conditionAddress));
        }
        else
        {
            Address conditionAddress;
            m_codec->decode(data, conditionAddress);
            conditionPrecompiled =
                std::dynamic_pointer_cast<ConditionPrecompiled>(_context->getPrecompiled(
                    std::string((char*)conditionAddress.data(), conditionAddress.size)));
        }
        precompiled::Condition::Ptr entryCondition = conditionPrecompiled->getCondition();
        storage::Condition::Ptr keyCondition = std::make_shared<storage::Condition>();
        std::vector<std::string> eqKeyList;
        bool findKeyFlag = false;
        for (auto& cond : entryCondition->m_conditions)
        {
            if (cond.left == m_table->tableInfo()->key)
            {
                findKeyFlag = true;
                if (cond.cmp == precompiled::Comparator::EQ)
                {
                    eqKeyList.emplace_back(cond.right);
                    continue;
                }
                else
                {
                    transferKeyCond(cond, keyCondition);
                }
            }
        }
        if (!findKeyFlag)
        {
            PRECOMPILED_LOG(ERROR)
                    << LOG_BADGE("TablePrecompiled") << LOG_BADGE("SELECT")
                    << LOG_DESC("can't get any primary key in condition")
                    << LOG_KV("primaryKey", m_table->tableInfo()->key);
            callResult->setExecResult(m_codec->encode(u256((int)CODE_INVALID_UPDATE_TABLE_KEY)));
        }
        else
        {
            auto tableKeyList = m_table->getPrimaryKeys(keyCondition);
            std::set<std::string> tableKeySet{tableKeyList.begin(), tableKeyList.end()};
            tableKeySet.insert(eqKeyList.begin(), eqKeyList.end());
            auto entries = std::make_shared<precompiled::Entries>();
            for (auto& key : tableKeySet)
            {
                auto entry = m_table->getRow(key);
                if (entryCondition->filter(entry))
                {
                    entries->emplace_back(entry);
                }
            }
            // update the memory gas and the computation gas
            gasPricer->updateMemUsed(getEntriesCapacity(entries));
            gasPricer->appendOperation(InterfaceOpcode::Select, entries->size());

            auto entriesPrecompiled = std::make_shared<EntriesPrecompiled>(m_hashImpl);
            entriesPrecompiled->setEntries(entries);

            auto newAddress = _context->registerPrecompiled(entriesPrecompiled);
            callResult->setExecResult(m_codec->encode(newAddress));
        }
    }
    else if (func == name2Selector[TABLE_METHOD_INS_STR_ADD])
    {
        // insert(address)
        if (!checkAuthority(_context->getTableFactory(), _origin, _sender))
        {
            PRECOMPILED_LOG(ERROR)
                    << LOG_BADGE("TablePrecompiled") << LOG_DESC("permission denied")
                    << LOG_KV("origin", _origin) << LOG_KV("contract", _sender);
            BOOST_THROW_EXCEPTION(
                PrecompiledError() << errinfo_comment("Permission denied. " + _origin + " can't call contract " + _sender));
        }
        EntryPrecompiled::Ptr entryPrecompiled = nullptr;
        std::string key;
        if (_context->isWasm())
        {
            std::string entryAddress;
            m_codec->decode(data, key, entryAddress);
            entryPrecompiled =
                std::dynamic_pointer_cast<EntryPrecompiled>(_context->getPrecompiled(entryAddress));
        }
        else
        {
            Address entryAddress;
            m_codec->decode(data, key, entryAddress);
            entryPrecompiled = std::dynamic_pointer_cast<EntryPrecompiled>(_context->getPrecompiled(
                std::string((char*)entryAddress.data(), entryAddress.size)));
        }
        PRECOMPILED_LOG(DEBUG) << LOG_DESC("Table insert") << LOG_KV("key", key);

        auto entry = entryPrecompiled->getEntry();
        checkLengthValidate(
            key, USER_TABLE_KEY_VALUE_MAX_LENGTH, CODE_TABLE_KEY_VALUE_LENGTH_OVERFLOW);
        auto it = entry->begin();
        std::string findKeyValue;
        for (; it != entry->end(); ++it)
        {
            checkLengthValidate(it->second, USER_TABLE_FIELD_VALUE_MAX_LENGTH,
                CODE_TABLE_KEY_VALUE_LENGTH_OVERFLOW);
            if (it->first == m_table->tableInfo()->key)
            {
                findKeyValue = it->second;
            }
        }
        if (findKeyValue.empty())
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("INSERT")
                                   << LOG_DESC("can't get any primary key in entry string")
                                   << LOG_KV("primaryKey", m_table->tableInfo()->key);
            callResult->setExecResult(m_codec->encode(u256((int)CODE_INVALID_UPDATE_TABLE_KEY)));
        }
        else
        {
            m_table->setRow(findKeyValue, entry);
            auto commitResult = _context->getTableFactory()->commit();
            if (!commitResult.second ||
                commitResult.second->errorCode() == protocol::CommonError::SUCCESS)
            {
                gasPricer->appendOperation(InterfaceOpcode::Insert, commitResult.first);
                gasPricer->updateMemUsed(entry->capacityOfHashField() * commitResult.first);
            }
            callResult->setExecResult(m_codec->encode(u256(commitResult.first)));
        }
    }
    else if (func == name2Selector[TABLE_METHOD_NEW_COND])
    {  // newCondition()
        auto condition = std::make_shared<precompiled::Condition>();
        auto conditionPrecompiled = std::make_shared<ConditionPrecompiled>(m_hashImpl);
        conditionPrecompiled->setCondition(condition);

        if (_context->isWasm())
        {
            std::string newAddress = _context->registerPrecompiled(conditionPrecompiled);
            callResult->setExecResult(m_codec->encode(newAddress));
        }
        else
        {
            Address newAddress = Address(_context->registerPrecompiled(conditionPrecompiled), FixedBytes<20>::FromBinary);
            callResult->setExecResult(m_codec->encode(newAddress));
        }
    }
    else if (func == name2Selector[TABLE_METHOD_NEW_ENTRY])
    {  // newEntry()
        auto entry = m_table->newEntry();
        auto entryPrecompiled = std::make_shared<EntryPrecompiled>(m_hashImpl);
        entryPrecompiled->setEntry(entry);

        if (_context->isWasm())
        {
            std::string newAddress = _context->registerPrecompiled(entryPrecompiled);
            callResult->setExecResult(m_codec->encode(newAddress));
        }
        else
        {
            Address newAddress = Address(_context->registerPrecompiled(entryPrecompiled), FixedBytes<20>::FromBinary);
            callResult->setExecResult(m_codec->encode(newAddress));
        }
    }
    else if (func == name2Selector[TABLE_METHOD_RE_STR_ADD])
    {
        // remove(address)
        if (checkAuthority(_context->getTableFactory(), _origin, _sender))
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("TablePrecompiled") << LOG_DESC("permission denied")
                                   << LOG_KV("origin", _origin) << LOG_KV("contract", _sender);
            BOOST_THROW_EXCEPTION(
                protocol::PrecompiledError() << errinfo_comment(
                    "Permission denied. " + _origin + " can't call contract " + _sender));
        }
        std::string key;
        ConditionPrecompiled::Ptr conditionPrecompiled = nullptr;
        if (_context->isWasm())
        {
            std::string conditionAddress;
            m_codec->decode(data, key, conditionAddress);
            conditionPrecompiled = std::dynamic_pointer_cast<ConditionPrecompiled>(
                _context->getPrecompiled(conditionAddress));
        }
        else
        {
            Address conditionAddress;
            m_codec->decode(data, key, conditionAddress);
            conditionPrecompiled =
                std::dynamic_pointer_cast<ConditionPrecompiled>(_context->getPrecompiled(
                    std::string((char*)conditionAddress.data(), conditionAddress.size)));
        }
        PRECOMPILED_LOG(DEBUG) << LOG_DESC("Table remove") << LOG_KV("key", key);
        precompiled::Condition::Ptr entryCondition = conditionPrecompiled->getCondition();
        storage::Condition::Ptr keyCondition = std::make_shared<storage::Condition>();
        std::vector<std::string> eqKeyList;
        bool findKeyFlag = false;
        for (auto& cond : entryCondition->m_conditions)
        {
            if (cond.left == m_table->tableInfo()->key)
            {
                findKeyFlag = true;
                if (cond.cmp == precompiled::Comparator::EQ)
                {
                    eqKeyList.emplace_back(cond.right);
                    continue;
                }
                else
                {
                    transferKeyCond(cond, keyCondition);
                }
            }
        }
        if (!findKeyFlag)
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("REMOVE")
                                   << LOG_DESC("can't get any primary key in condition")
                                   << LOG_KV("primaryKey", m_table->tableInfo()->key);
            callResult->setExecResult(m_codec->encode(u256((int)CODE_INVALID_UPDATE_TABLE_KEY)));
        }
        else
        {
            auto tableKeyList = m_table->getPrimaryKeys(keyCondition);
            std::set<std::string> tableKeySet{tableKeyList.begin(), tableKeyList.end()};
            tableKeySet.insert(eqKeyList.begin(), eqKeyList.end());
            for (auto& tableKey : tableKeySet)
            {
                auto entry = m_table->getRow(key);
                if (entryCondition->filter(entry))
                {
                    m_table->remove(tableKey);
                }
            }
            auto commitResult = _context->getTableFactory()->commit();
            if (!commitResult.second ||
                commitResult.second->errorCode() == protocol::CommonError::SUCCESS)
            {
                gasPricer->appendOperation(InterfaceOpcode::Remove, commitResult.first);
            }
            callResult->setExecResult(m_codec->encode(u256(commitResult.first)));
        }
    }
    else if (func == name2Selector[TABLE_METHOD_UP_STR_2ADD])
    {
        // update(address,address)
        if (checkAuthority(_context->getTableFactory(), _origin, _sender))
        {
            PRECOMPILED_LOG(ERROR)
                    << LOG_BADGE("TablePrecompiled") << LOG_DESC("permission denied")
                    << LOG_KV("origin", _origin) << LOG_KV("contract", _sender);
            BOOST_THROW_EXCEPTION(
                protocol::PrecompiledError() << errinfo_comment("Permission denied. " + _origin + " can't call contract " + _sender));
        }
        std::string key;
        EntryPrecompiled::Ptr entryPrecompiled = nullptr;
        ConditionPrecompiled::Ptr conditionPrecompiled = nullptr;
        if (_context->isWasm())
        {
            std::string entryAddress;
            std::string conditionAddress;
            m_codec->decode(data, key, entryAddress, conditionAddress);
            entryPrecompiled =
                std::dynamic_pointer_cast<EntryPrecompiled>(_context->getPrecompiled(entryAddress));
            conditionPrecompiled = std::dynamic_pointer_cast<ConditionPrecompiled>(
                _context->getPrecompiled(conditionAddress));
        }
        else
        {
            Address entryAddress;
            Address conditionAddress;
            m_codec->decode(data, key, entryAddress, conditionAddress);
            entryPrecompiled = std::dynamic_pointer_cast<EntryPrecompiled>(_context->getPrecompiled(
                std::string((char*)entryAddress.data(), entryAddress.size)));
            conditionPrecompiled =
                std::dynamic_pointer_cast<ConditionPrecompiled>(_context->getPrecompiled(
                    std::string((char*)conditionAddress.data(), conditionAddress.size)));
        }
        PRECOMPILED_LOG(DEBUG) << LOG_DESC("Table update") << LOG_KV("key", key);
        auto entry = entryPrecompiled->getEntry();

        std::string findKeyValue;
        auto it = entry->begin();
        for (; it != entry->end(); ++it)
        {
            checkLengthValidate(it->second, USER_TABLE_FIELD_VALUE_MAX_LENGTH,
                CODE_TABLE_FIELD_VALUE_LENGTH_OVERFLOW);
            if (it->first == m_table->tableInfo()->key)
            {
                findKeyValue = it->second;
            }
        }
        if (findKeyValue.empty())
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("UPDATE")
                                   << LOG_DESC("can't get any primary key in entry string")
                                   << LOG_KV("primaryKey", m_table->tableInfo()->key);
            callResult->setExecResult(m_codec->encode(u256((int)CODE_INVALID_UPDATE_TABLE_KEY)));
        }
        else
        {
            auto entryCondition = conditionPrecompiled->getCondition();
            storage::Condition::Ptr keyCondition = std::make_shared<storage::Condition>();
            std::vector<std::string> eqKeyList;
            bool findKeyFlag = false;
            for (auto& cond : entryCondition->m_conditions)
            {
                if (cond.left == m_table->tableInfo()->key)
                {
                    findKeyFlag = true;
                    if (cond.cmp == precompiled::Comparator::EQ)
                    {
                        eqKeyList.emplace_back(cond.right);
                        continue;
                    }
                    else
                    {
                        transferKeyCond(cond, keyCondition);
                    }
                }
            }
            if (!findKeyFlag)
            {
                PRECOMPILED_LOG(ERROR) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("UPDATE")
                                       << LOG_DESC("can't get any primary key in condition")
                                       << LOG_KV("primaryKey", m_table->tableInfo()->key);
                callResult->setExecResult(
                    m_codec->encode(u256((int)CODE_INVALID_UPDATE_TABLE_KEY)));
            }
            else
            {
                auto tableKeyList = m_table->getPrimaryKeys(keyCondition);
                std::set<std::string> tableKeySet{tableKeyList.begin(), tableKeyList.end()};
                tableKeySet.insert(eqKeyList.begin(), eqKeyList.end());
                for (auto& tableKey : tableKeySet)
                {
                    auto tableEntry = m_table->getRow(tableKey);
                    if(entryCondition->filter(tableEntry))
                    {
                        m_table->setRow(tableKey, entry);
                    }
                }
                auto commitResult = _context->getTableFactory()->commit();
                if (!commitResult.second ||
                    commitResult.second->errorCode() == protocol::CommonError::SUCCESS)
                {
                    gasPricer->setMemUsed(entry->capacityOfHashField() * commitResult.first);
                    gasPricer->appendOperation(InterfaceOpcode::Update, commitResult.first);
                }
                callResult->setExecResult(m_codec->encode(u256(commitResult.first)));
                PRECOMPILED_LOG(DEBUG) << LOG_DESC("Table update") << LOG_KV("ret", commitResult.first);
            }
        }
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TablePrecompiled")
                               << LOG_DESC("call undefined function!");
    }
    gasPricer->updateMemUsed(callResult->m_execResult.size());
    _remainGas -= gasPricer->calTotalGas();
    return callResult;
}