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

const char* const TABLE_METHOD_SLT_STR_ADD = "select(address)";
const char* const TABLE_METHOD_INS_STR_ADD = "insert(address)";
const char* const TABLE_METHOD_NEW_COND = "newCondition()";
const char* const TABLE_METHOD_NEW_ENTRY = "newEntry()";
const char* const TABLE_METHOD_RE_STR_ADD = "remove(address)";
const char* const TABLE_METHOD_UP_STR_2ADD = "update(address,address)";

// specific method for wasm
const char* const TABLE_METHOD_SLT_STR_WASM = "select(string)";
const char* const TABLE_METHOD_INS_STR_WASM = "insert(string)";
const char* const TABLE_METHOD_RE_STR_WASM = "remove(string)";
const char* const TABLE_METHOD_UP_STR_WASM = "update(string,string)";

TablePrecompiled::TablePrecompiled(crypto::Hash::Ptr _hashImpl) : Precompiled(_hashImpl)
{
    name2Selector[TABLE_METHOD_SLT_STR_ADD] = getFuncSelector(TABLE_METHOD_SLT_STR_ADD, _hashImpl);
    name2Selector[TABLE_METHOD_INS_STR_ADD] = getFuncSelector(TABLE_METHOD_INS_STR_ADD, _hashImpl);
    name2Selector[TABLE_METHOD_NEW_COND] = getFuncSelector(TABLE_METHOD_NEW_COND, _hashImpl);
    name2Selector[TABLE_METHOD_NEW_ENTRY] = getFuncSelector(TABLE_METHOD_NEW_ENTRY, _hashImpl);
    name2Selector[TABLE_METHOD_RE_STR_ADD] = getFuncSelector(TABLE_METHOD_RE_STR_ADD, _hashImpl);
    name2Selector[TABLE_METHOD_UP_STR_2ADD] = getFuncSelector(TABLE_METHOD_UP_STR_2ADD, _hashImpl);

    name2Selector[TABLE_METHOD_SLT_STR_WASM] =
        getFuncSelector(TABLE_METHOD_SLT_STR_WASM, _hashImpl);
    name2Selector[TABLE_METHOD_INS_STR_WASM] =
        getFuncSelector(TABLE_METHOD_INS_STR_WASM, _hashImpl);
    name2Selector[TABLE_METHOD_RE_STR_WASM] = getFuncSelector(TABLE_METHOD_RE_STR_WASM, _hashImpl);
    name2Selector[TABLE_METHOD_UP_STR_WASM] = getFuncSelector(TABLE_METHOD_UP_STR_WASM, _hashImpl);
}

std::string TablePrecompiled::toString()
{
    return "Table";
}

PrecompiledExecResult::Ptr TablePrecompiled::call(std::shared_ptr<executor::BlockContext> _context,
    bytesConstRef _param, const std::string& _origin, const std::string& _sender,
    int64_t _remainGas)
{
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);

    auto codec = std::make_shared<PrecompiledCodec>(_context->hashHandler(), _context->isWasm());
    codec = std::make_shared<PrecompiledCodec>(nullptr, _context->isWasm());
    auto callResult = std::make_shared<PrecompiledExecResult>();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();
    gasPricer->setMemUsed(_param.size());

    if (func == name2Selector[TABLE_METHOD_SLT_STR_ADD] ||
        func == name2Selector[TABLE_METHOD_SLT_STR_WASM])
    {
        // select(address) || select(string)
        ConditionPrecompiled::Ptr conditionPrecompiled = nullptr;
        if (_context->isWasm())
        {
            // wasm env
            std::string conditionAddress;
            codec->decode(data, conditionAddress);
            conditionPrecompiled = std::dynamic_pointer_cast<ConditionPrecompiled>(
                _context->getPrecompiled(conditionAddress));
        }
        else
        {
            // evm env
            Address conditionAddress;
            codec->decode(data, conditionAddress);
            conditionPrecompiled = std::dynamic_pointer_cast<ConditionPrecompiled>(
                _context->getPrecompiled(conditionAddress.hex()));
        }

        precompiled::Condition::Ptr entryCondition = conditionPrecompiled->getCondition();
        std::shared_ptr<storage::Condition> keyCondition = std::make_shared<storage::Condition>();
        std::vector<std::string> eqKeyList;
        bool findKeyFlag = false;
        for (auto& cond : entryCondition->m_conditions)
        {
            if (cond.left == m_keyField)
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
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("SELECT")
                                   << LOG_DESC("can't get any primary key in condition");
            auto entriesPrecompiled = std::make_shared<EntriesPrecompiled>(m_hashImpl);
            auto entries = std::make_shared<precompiled::Entries>();
            entriesPrecompiled->setEntries(entries);
            if (_context->isWasm())
            {
                // wasm env
                auto newAddress = _context->registerPrecompiled(entriesPrecompiled);
                callResult->setExecResult(codec->encode(newAddress));
            }
            else
            {
                // evm env
                auto newAddress = Address(_context->registerPrecompiled(entriesPrecompiled));
                callResult->setExecResult(codec->encode(newAddress));
            }
        }
        else
        {
            // merge keys from storage and eqKeys
            auto tableKeyList = m_table->getPrimaryKeys(*keyCondition);
            std::set<std::string> tableKeySet{tableKeyList.begin(), tableKeyList.end()};
            tableKeySet.insert(eqKeyList.begin(), eqKeyList.end());
            auto entries = std::make_shared<precompiled::Entries>();
            for (auto& key : tableKeySet)
            {
                auto entry = m_table->getRow(key);
                if (!entry)
                    continue;
                auto entryPtr = std::make_shared<storage::Entry>(entry.value());
                if (entryCondition->filter(entry))
                {
                    entries->emplace_back(entryPtr);
                }
            }
            // update the memory gas and the computation gas
            gasPricer->updateMemUsed(getEntriesCapacity(entries));
            gasPricer->appendOperation(InterfaceOpcode::Select, entries->size());

            auto entriesPrecompiled = std::make_shared<EntriesPrecompiled>(m_hashImpl);
            entriesPrecompiled->setEntries(entries);
            if (_context->isWasm())
            {
                // wasm env
                auto newAddress = _context->registerPrecompiled(entriesPrecompiled);
                callResult->setExecResult(codec->encode(newAddress));
            }
            else
            {
                // evm env
                auto newAddress = Address(_context->registerPrecompiled(entriesPrecompiled));
                callResult->setExecResult(codec->encode(newAddress));
            }
        }
    }
    else if (func == name2Selector[TABLE_METHOD_INS_STR_ADD] ||
             func == name2Selector[TABLE_METHOD_INS_STR_WASM])
    {
        // insert(address) || insert(string)
        EntryPrecompiled::Ptr entryPrecompiled = nullptr;
        if (_context->isWasm())
        {
            // wasm env
            std::string entryAddress;
            codec->decode(data, entryAddress);
            entryPrecompiled =
                std::dynamic_pointer_cast<EntryPrecompiled>(_context->getPrecompiled(entryAddress));
        }
        else
        {
            // evm env
            Address entryAddress;
            codec->decode(data, entryAddress);
            entryPrecompiled = std::dynamic_pointer_cast<EntryPrecompiled>(
                _context->getPrecompiled(entryAddress.hex()));
        }

        auto entry = entryPrecompiled->getEntry();
        std::string findKeyValue;
        // check entry value validate
        for (auto it = entry->begin(); it != entry->end(); ++it)
        {
            checkLengthValidate(static_cast<const std::string>(*it),
                USER_TABLE_FIELD_VALUE_MAX_LENGTH, CODE_TABLE_KEY_VALUE_LENGTH_OVERFLOW);
        }
        // FIXME: entry not contains key field
        //        for (auto it = entry->tableInfo()->fields().cbegin();
        //             it != entry->tableInfo()->fields().cend(); it++)
        //        {
        //            if (*it == m_keyField)
        //            {
        //                findKeyValue = it->second;
        //            }
        //        }
        if (findKeyValue.empty())
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("INSERT")
                                   << LOG_DESC("can't get any primary key in entry string")
                                   << LOG_KV("primaryKey", m_keyField);
            getErrorCodeOut(callResult->mutableExecResult(), CODE_KEY_NOT_EXIST_IN_ENTRY, codec);
        }
        else
        {
            PRECOMPILED_LOG(DEBUG) << LOG_DESC("Table insert") << LOG_KV("key", findKeyValue);
            auto checkExistEntry = m_table->getRow(findKeyValue);
            if (checkExistEntry)
            {
                PRECOMPILED_LOG(ERROR)
                    << LOG_BADGE("TablePrecompiled") << LOG_BADGE("INSERT")
                    << LOG_DESC("key already exist in table, please use UPDATE method")
                    << LOG_KV("primaryKey", m_keyField) << LOG_KV("existKey", findKeyValue);
                getErrorCodeOut(callResult->mutableExecResult(), CODE_INSERT_KEY_EXIST, codec);
            }
            else
            {
                m_table->setRow(findKeyValue, *entry);
                gasPricer->appendOperation(InterfaceOpcode::Insert, 1);
                gasPricer->updateMemUsed(entry->capacityOfHashField());
                callResult->setExecResult(codec->encode(u256(1)));
            }
        }
    }
    else if (func == name2Selector[TABLE_METHOD_NEW_COND])
    {
        // newCondition()
        auto condition = std::make_shared<precompiled::Condition>();
        auto conditionPrecompiled = std::make_shared<ConditionPrecompiled>(m_hashImpl);
        conditionPrecompiled->setCondition(condition);

        if (_context->isWasm())
        {
            // wasm env
            std::string newAddress = _context->registerPrecompiled(conditionPrecompiled);
            callResult->setExecResult(codec->encode(newAddress));
        }
        else
        {
            // evm env
            Address newAddress = Address(_context->registerPrecompiled(conditionPrecompiled));
            callResult->setExecResult(codec->encode(newAddress));
        }
    }
    else if (func == name2Selector[TABLE_METHOD_NEW_ENTRY])
    {
        // newEntry()
        auto entry = m_table->newEntry();
        auto entryPrecompiled = std::make_shared<EntryPrecompiled>(m_hashImpl);
        entryPrecompiled->setEntry(std::make_shared<storage::Entry>(entry));

        if (_context->isWasm())
        {
            // wasm env
            std::string newAddress = _context->registerPrecompiled(entryPrecompiled);
            callResult->setExecResult(codec->encode(newAddress));
        }
        else
        {
            // evm env
            Address newAddress = Address(_context->registerPrecompiled(entryPrecompiled));
            callResult->setExecResult(codec->encode(newAddress));
        }
    }
    else if (func == name2Selector[TABLE_METHOD_RE_STR_ADD] ||
             func == name2Selector[TABLE_METHOD_RE_STR_WASM])
    {
        // remove(address) || remove(string)
        ConditionPrecompiled::Ptr conditionPrecompiled = nullptr;
        if (_context->isWasm())
        {
            // wasm env
            std::string conditionAddress;
            codec->decode(data, conditionAddress);
            conditionPrecompiled = std::dynamic_pointer_cast<ConditionPrecompiled>(
                _context->getPrecompiled(conditionAddress));
        }
        else
        {
            // evm env
            Address conditionAddress;
            codec->decode(data, conditionAddress);
            conditionPrecompiled = std::dynamic_pointer_cast<ConditionPrecompiled>(
                _context->getPrecompiled(conditionAddress.hex()));
        }

        precompiled::Condition::Ptr entryCondition = conditionPrecompiled->getCondition();
        std::shared_ptr<storage::Condition> keyCondition = std::make_shared<storage::Condition>();
        std::vector<std::string> eqKeyList;
        bool findKeyFlag = false;
        for (auto& cond : entryCondition->m_conditions)
        {
            if (cond.left == m_keyField)
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
                                   << LOG_KV("primaryKey", m_keyField);
            getErrorCodeOut(callResult->mutableExecResult(), CODE_KEY_NOT_EXIST_IN_ENTRY, codec);
        }
        else
        {
            auto tableKeyList = m_table->getPrimaryKeys(*keyCondition);
            std::set<std::string> tableKeySet{tableKeyList.begin(), tableKeyList.end()};
            tableKeySet.insert(eqKeyList.begin(), eqKeyList.end());
            for (auto& tableKey : tableKeySet)
            {
                auto entry = m_table->getRow(tableKey);
                // Note: entry maybe nullptr
                if (entryCondition->filter(entry))
                {
                    m_table->setRow(tableKey, m_table->newDeletedEntry());
                }
            }
            gasPricer->appendOperation(InterfaceOpcode::Remove, 1);
            callResult->setExecResult(codec->encode(u256(1)));
        }
    }
    else if (func == name2Selector[TABLE_METHOD_UP_STR_2ADD] ||
             func == name2Selector[TABLE_METHOD_UP_STR_WASM])
    {
        // update(address,address) || update(string,string)
        EntryPrecompiled::Ptr entryPrecompiled = nullptr;
        ConditionPrecompiled::Ptr conditionPrecompiled = nullptr;
        if (_context->isWasm())
        {
            std::string entryAddress;
            std::string conditionAddress;
            codec->decode(data, entryAddress, conditionAddress);
            entryPrecompiled =
                std::dynamic_pointer_cast<EntryPrecompiled>(_context->getPrecompiled(entryAddress));
            conditionPrecompiled = std::dynamic_pointer_cast<ConditionPrecompiled>(
                _context->getPrecompiled(conditionAddress));
        }
        else
        {
            Address entryAddress;
            Address conditionAddress;
            codec->decode(data, entryAddress, conditionAddress);
            entryPrecompiled = std::dynamic_pointer_cast<EntryPrecompiled>(
                _context->getPrecompiled(entryAddress.hex()));
            conditionPrecompiled = std::dynamic_pointer_cast<ConditionPrecompiled>(
                _context->getPrecompiled(conditionAddress.hex()));
        }
        auto entry = entryPrecompiled->getEntry();
        auto entryCondition = conditionPrecompiled->getCondition();
        std::shared_ptr<storage::Condition> keyCondition = std::make_shared<storage::Condition>();
        std::vector<std::string> eqKeyList;
        bool findKeyFlag = false;
        for (auto& cond : entryCondition->m_conditions)
        {
            if (cond.left == m_keyField)
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
                                   << LOG_KV("primaryKey", m_keyField);
            getErrorCodeOut(callResult->mutableExecResult(), CODE_KEY_NOT_EXIST_IN_COND, codec);
        }
        else
        {
            bool eqKeyExist = true;
            // check eq key exist in table
            for (auto& key : eqKeyList)
            {
                auto checkExistEntry = m_table->getRow(key);
                if (!checkExistEntry)
                {
                    PRECOMPILED_LOG(ERROR)
                        << LOG_BADGE("TablePrecompiled") << LOG_BADGE("UPDATE")
                        << LOG_DESC("key not exist in table, please use INSERT method")
                        << LOG_KV("primaryKey", m_keyField) << LOG_KV("notExistKey", key);
                    eqKeyExist = false;
                    getErrorCodeOut(
                        callResult->mutableExecResult(), CODE_UPDATE_KEY_NOT_EXIST, codec);
                    break;
                }
            }
            if (eqKeyExist)
            {
                auto tableKeyList = m_table->getPrimaryKeys(*keyCondition);
                std::set<std::string> tableKeySet{tableKeyList.begin(), tableKeyList.end()};
                tableKeySet.insert(eqKeyList.begin(), eqKeyList.end());
                for (auto& tableKey : tableKeySet)
                {
                    auto tableEntry = m_table->getRow(tableKey);
                    if (entryCondition->filter(tableEntry))
                    {
                        m_table->setRow(tableKey, *entry);
                    }
                }
                gasPricer->setMemUsed(entry->capacityOfHashField());
                gasPricer->appendOperation(InterfaceOpcode::Update, tableKeySet.size());
                callResult->setExecResult(codec->encode(u256(1)));
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