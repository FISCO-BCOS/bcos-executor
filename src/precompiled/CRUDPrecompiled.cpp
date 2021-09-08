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
 * @file CRUDPrecompiled.cpp
 * @author: kyonRay
 * @date 2021-05-31
 */

#include "CRUDPrecompiled.h"
#include "Common.h"
#include "PrecompiledResult.h"
#include "Utilities.h"
#include <bcos-framework/interfaces/protocol/CommonError.h>
#include <bcos-framework/interfaces/storage/Table.h>
#include <json/json.h>
#include <boost/lexical_cast.hpp>

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::precompiled;

/*
contract CRUDPrecompiled {
    function insert(string tableName, string entry, string) public returns (bool);
    function remove(string tableName, string condition, string) public returns (int256);
    function select(string tableName, string condition, string) public view returns (string);
    function update(string tableName, string entry, string condition, string) public returns
(int256); function desc(string tableName) public view returns (string, string);
}
 */
const char* const CRUD_METHOD_INSERT_STR = "insert(string,string,string)";
const char* const CRUD_METHOD_REMOVE_STR = "remove(string,string,string)";
const char* const CRUD_METHOD_UPDATE_STR = "update(string,string,string,string)";
const char* const CRUD_METHOD_SELECT_STR = "select(string,string,string)";
const char* const CRUD_METHOD_DESC_STR = "desc(string)";

CRUDPrecompiled::CRUDPrecompiled(crypto::Hash::Ptr _hashImpl) : Precompiled(_hashImpl)
{
    name2Selector[CRUD_METHOD_INSERT_STR] = getFuncSelector(CRUD_METHOD_INSERT_STR, _hashImpl);
    name2Selector[CRUD_METHOD_REMOVE_STR] = getFuncSelector(CRUD_METHOD_REMOVE_STR, _hashImpl);
    name2Selector[CRUD_METHOD_UPDATE_STR] = getFuncSelector(CRUD_METHOD_UPDATE_STR, _hashImpl);
    name2Selector[CRUD_METHOD_SELECT_STR] = getFuncSelector(CRUD_METHOD_SELECT_STR, _hashImpl);
    name2Selector[CRUD_METHOD_DESC_STR] = getFuncSelector(CRUD_METHOD_DESC_STR, _hashImpl);
}

std::string CRUDPrecompiled::toString()
{
    return "CRUD";
}

std::shared_ptr<PrecompiledExecResult> CRUDPrecompiled::call(
    std::shared_ptr<executor::BlockContext> _context, bytesConstRef _param, const std::string&,
    const std::string&, u256& _remainGas)
{
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);

    auto callResult = std::make_shared<PrecompiledExecResult>();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();
    gasPricer->setMemUsed(_param.size());
    if (func == name2Selector[CRUD_METHOD_DESC_STR])
    {  // desc(string)
        desc(_context, data, callResult, gasPricer);
    }
    else if (func == name2Selector[CRUD_METHOD_INSERT_STR])
    {
        // insert(string tableName, string entry, string optional)
        insert(_context, data, callResult, gasPricer);
    }
    else if (func == name2Selector[CRUD_METHOD_UPDATE_STR])
    {
        // update(string tableName, string entry, string condition, string optional)
        update(_context, data, callResult, gasPricer);
    }
    else if (func == name2Selector[CRUD_METHOD_REMOVE_STR])
    {
        // remove(string tableName, string condition, string optional)
        remove(_context, data, callResult, gasPricer);
    }
    else if (func == name2Selector[CRUD_METHOD_SELECT_STR])
    {
        select(_context, data, callResult, gasPricer);
    }
    else
    {
        codec::abi::ContractABICodec abi(nullptr);
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("CRUDPrecompiled")
                               << LOG_DESC("call undefined function") << LOG_KV("func", func);
        callResult->setExecResult(abi.abiIn("", u256((int)CODE_UNKNOW_FUNCTION_CALL)));
    }
    gasPricer->updateMemUsed(callResult->m_execResult.size());
    _remainGas -= gasPricer->calTotalGas();
    return callResult;
}

void CRUDPrecompiled::desc(std::shared_ptr<executor::BlockContext> _context,
    bytesConstRef _paramData, PrecompiledExecResult::Ptr _callResult,
    std::shared_ptr<PrecompiledGas> _gasPricer)
{
    auto codec = std::make_shared<PrecompiledCodec>(_context->hashHandler(), _context->isWasm());
    std::string tableName;
    codec->decode(_paramData, tableName);
    tableName = precompiled::getTableName(tableName);

    // s_tables must exist
    auto table = _context->getTableFactory()->openTable(SYS_TABLE);
    _gasPricer->appendOperation(InterfaceOpcode::OpenTable);

    auto entry = table->getRow(tableName);
    std::string keyField, valueField;
    if (entry)
    {
        _gasPricer->appendOperation(InterfaceOpcode::Select, entry->capacityOfHashField());
        keyField = entry->getField(SYS_TABLE_KEY_FIELDS);
        valueField = entry->getField(SYS_TABLE_VALUE_FIELDS);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("CRUDPrecompiled") << LOG_BADGE("DESC")
                               << LOG_DESC("table not exist") << LOG_KV("tableName", tableName);
    }
    _callResult->setExecResult(codec->encode(keyField, valueField));
}

void CRUDPrecompiled::update(std::shared_ptr<executor::BlockContext> _context,
    bytesConstRef _paramData, PrecompiledExecResult::Ptr _callResult,
    std::shared_ptr<PrecompiledGas> _gasPricer)
{
    auto codec = std::make_shared<PrecompiledCodec>(_context->hashHandler(), _context->isWasm());
    std::string tableName, entryStr, conditionStr, optional;
    codec->decode(_paramData, tableName, entryStr, conditionStr, optional);
    tableName = precompiled::getTableName(tableName);
    auto table = _context->getTableFactory()->openTable(tableName);
    _gasPricer->appendOperation(InterfaceOpcode::OpenTable);
    if (table)
    {
        Entry::Ptr entry = table->newEntry();
        int parseEntryResult = parseEntry(entryStr, entry);
        if (parseEntryResult != CODE_SUCCESS)
        {
            getErrorCodeOut(_callResult->mutableExecResult(), parseEntryResult, codec);
            return;
        }
        // check the entry
        auto it = entry->begin();
        for (; it != entry->end(); ++it)
        {
            checkLengthValidate(it->second, USER_TABLE_FIELD_VALUE_MAX_LENGTH,
                CODE_TABLE_KEY_VALUE_LENGTH_OVERFLOW);
            if (it->first == table->tableInfo()->key)
            {
                PRECOMPILED_LOG(ERROR)
                    << LOG_BADGE("CRUDPrecompiled") << LOG_BADGE("UPDATE")
                    << LOG_DESC("can't update the key in entry") << LOG_KV("table", tableName)
                    << LOG_KV("key", table->tableInfo()->key);
                getErrorCodeOut(
                    _callResult->mutableExecResult(), CODE_INVALID_UPDATE_TABLE_KEY, codec);
                return;
            }
        }

        auto condition = std::make_shared<precompiled::Condition>();
        int parseConditionResult = parseCondition(conditionStr, condition, _gasPricer);
        if (parseConditionResult != CODE_SUCCESS)
        {
            getErrorCodeOut(_callResult->mutableExecResult(), parseConditionResult, codec);
            return;
        }

        std::shared_ptr<storage::Condition> keyCondition = std::make_shared<storage::Condition>();
        std::vector<std::string> eqKeyList;
        bool findKeyFlag = false;
        for (auto& cond : condition->m_conditions)
        {
            if (cond.left == table->tableInfo()->key)
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
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("CRUDPrecompiled") << LOG_BADGE("UPDATE")
                                   << LOG_DESC("can't get any primary key in condition")
                                   << LOG_KV("primaryKey", table->tableInfo()->key);
            getErrorCodeOut(_callResult->mutableExecResult(), CODE_KEY_NOT_EXIST_IN_COND, codec);
            return;
        }
        bool eqKeyExist = true;
        // check eq key exist in table
        for (auto& key : eqKeyList)
        {
            auto checkExistEntry = table->getRow(key);
            if (!checkExistEntry)
            {
                PRECOMPILED_LOG(ERROR)
                    << LOG_BADGE("CRUDPrecompiled") << LOG_BADGE("UPDATE")
                    << LOG_DESC("key not exist in table, please use INSERT method")
                    << LOG_KV("primaryKey", table->tableInfo()->key) << LOG_KV("notExistKey", key);
                eqKeyExist = false;
                getErrorCodeOut(_callResult->mutableExecResult(), CODE_UPDATE_KEY_NOT_EXIST, codec);
                break;
            }
        }
        if (eqKeyExist)
        {
            auto tableKeyList = table->getPrimaryKeys(keyCondition);
            std::set<std::string> tableKeySet{tableKeyList.begin(), tableKeyList.end()};
            tableKeySet.insert(eqKeyList.begin(), eqKeyList.end());
            for (auto& tableKey : tableKeySet)
            {
                auto tableEntry = table->getRow(tableKey);
                if (condition->filter(tableEntry))
                {
                    table->setRow(tableKey, entry);
                }
            }
            _gasPricer->setMemUsed(entry->capacityOfHashField());
            _gasPricer->appendOperation(InterfaceOpcode::Update, tableKeySet.size());
            getErrorCodeOut(_callResult->mutableExecResult(), tableKeySet.size(), codec);
        }
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("CRUDPrecompiled") << LOG_BADGE("UPDATE")
                               << LOG_DESC("table open error") << LOG_KV("tableName", tableName);
        getErrorCodeOut(_callResult->mutableExecResult(), CODE_TABLE_NOT_EXIST, codec);
    }
}

void CRUDPrecompiled::insert(std::shared_ptr<executor::BlockContext> _context,
    bytesConstRef _paramData, PrecompiledExecResult::Ptr _callResult,
    std::shared_ptr<PrecompiledGas> _gasPricer)
{
    auto codec = std::make_shared<PrecompiledCodec>(_context->hashHandler(), _context->isWasm());
    std::string tableName, entryStr, optional;
    codec->decode(_paramData, tableName, entryStr, optional);

    tableName = precompiled::getTableName(tableName);
    auto table = _context->getTableFactory()->openTable(tableName);
    _gasPricer->appendOperation(InterfaceOpcode::OpenTable);
    if (table)
    {
        auto tableInfo = table->tableInfo();
        Entry::Ptr entry = table->newEntry();
        int parseEntryResult = parseEntry(entryStr, entry);
        if (parseEntryResult != CODE_SUCCESS)
        {
            getErrorCodeOut(_callResult->mutableExecResult(), parseEntryResult, codec);
            return;
        }
        // check entry
        auto it = entry->begin();
        std::string keyValue;
        for (; it != entry->end(); ++it)
        {
            checkLengthValidate(it->second, USER_TABLE_FIELD_VALUE_MAX_LENGTH,
                CODE_TABLE_KEY_VALUE_LENGTH_OVERFLOW);
            if (it->first == table->tableInfo()->key)
            {
                keyValue = it->second;
            }
        }
        if (keyValue.empty())
        {
            PRECOMPILED_LOG(ERROR)
                << LOG_BADGE("CRUDPrecompiled") << LOG_BADGE("INSERT")
                << LOG_DESC("can't find specific key in entry") << LOG_KV("table", tableName)
                << LOG_KV("key", table->tableInfo()->key);
            getErrorCodeOut(_callResult->mutableExecResult(), CODE_INVALID_UPDATE_TABLE_KEY, codec);
            return;
        }
        table->setRow(keyValue, entry);
        _gasPricer->appendOperation(InterfaceOpcode::Insert, 1);
        _gasPricer->updateMemUsed(entry->capacityOfHashField());
        _callResult->setExecResult(codec->encode(u256(1)));
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("CRUDPrecompiled") << LOG_DESC("table open error")
                               << LOG_KV("tableName", tableName);
        getErrorCodeOut(_callResult->mutableExecResult(), CODE_TABLE_NOT_EXIST, codec);
    }
}

void CRUDPrecompiled::remove(std::shared_ptr<executor::BlockContext> _context,
    bytesConstRef _paramData, PrecompiledExecResult::Ptr _callResult,
    std::shared_ptr<PrecompiledGas> _gasPricer)
{
    auto codec = std::make_shared<PrecompiledCodec>(_context->hashHandler(), _context->isWasm());
    std::string tableName, conditionStr, optional;
    codec->decode(_paramData, tableName, conditionStr, optional);
    tableName = precompiled::getTableName(tableName);
    auto table = _context->getTableFactory()->openTable(tableName);
    _gasPricer->appendOperation(InterfaceOpcode::OpenTable);
    if (table)
    {
        auto condition = std::make_shared<precompiled::Condition>();
        int parseConditionResult = parseCondition(conditionStr, condition, _gasPricer);
        if (parseConditionResult != CODE_SUCCESS)
        {
            getErrorCodeOut(_callResult->mutableExecResult(), parseConditionResult, codec);
            return;
        }

        std::shared_ptr<storage::Condition> keyCondition = std::make_shared<storage::Condition>();
        std::vector<std::string> eqKeyList;
        bool findKeyFlag = false;
        for (auto& cond : condition->m_conditions)
        {
            if (cond.left == table->tableInfo()->key)
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
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("CRUDPrecompiled") << LOG_BADGE("REMOVE")
                                   << LOG_DESC("can't get any primary key in condition")
                                   << LOG_KV("primaryKey", table->tableInfo()->key);
            getErrorCodeOut(_callResult->mutableExecResult(), CODE_KEY_NOT_EXIST_IN_COND, codec);
            return;
        }
        auto tableKeyList = table->getPrimaryKeys(keyCondition);
        std::set<std::string> tableKeySet{tableKeyList.begin(), tableKeyList.end()};
        tableKeySet.insert(eqKeyList.begin(), eqKeyList.end());
        for (auto& tableKey : tableKeySet)
        {
            auto entry = table->getRow(tableKey);
            if (condition->filter(entry))
            {
                table->remove(tableKey);
            }
        }
        _gasPricer->appendOperation(InterfaceOpcode::Remove, tableKeySet.size());
        _gasPricer->updateMemUsed(tableKeySet.size());
        getErrorCodeOut(_callResult->mutableExecResult(), tableKeySet.size(), codec);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("CRUDPrecompiled") << LOG_DESC("table open error")
                               << LOG_KV("tableName", tableName);
        getErrorCodeOut(_callResult->mutableExecResult(), CODE_TABLE_NOT_EXIST, codec);
    }
}

void CRUDPrecompiled::select(std::shared_ptr<executor::BlockContext> _context,
    bytesConstRef _paramData, PrecompiledExecResult::Ptr _callResult,
    std::shared_ptr<PrecompiledGas> _gasPricer)
{
    // select(string tableName, string condition, string optional)
    auto codec = std::make_shared<PrecompiledCodec>(_context->hashHandler(), _context->isWasm());
    std::string tableName, conditionStr, optional;
    codec->decode(_paramData, tableName, conditionStr, optional);
    if (tableName != SYS_TABLE)
    {
        tableName = precompiled::getTableName(tableName);
    }
    auto table = _context->getTableFactory()->openTable(tableName);
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();
    gasPricer->appendOperation(InterfaceOpcode::OpenTable);
    if (table)
    {
        auto condition = std::make_shared<precompiled::Condition>();
        int parseConditionResult = parseCondition(conditionStr, condition, _gasPricer);
        if (parseConditionResult != CODE_SUCCESS)
        {
            getErrorCodeOut(_callResult->mutableExecResult(), parseConditionResult, codec);
            return;
        }
        std::shared_ptr<storage::Condition> keyCondition = std::make_shared<storage::Condition>();
        std::vector<std::string> eqKeyList;
        bool findKeyFlag = false;
        for (auto& cond : condition->m_conditions)
        {
            if (cond.left == table->tableInfo()->key)
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
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("CRUDPrecompiled") << LOG_BADGE("SELECT")
                                   << LOG_DESC("can't get any primary key in condition")
                                   << LOG_KV("primaryKey", table->tableInfo()->key);
            getErrorCodeOut(_callResult->mutableExecResult(), CODE_KEY_NOT_EXIST_IN_COND, codec);
            return;
        }
        // merge keys from storage and eqKeys
        auto tableKeyList = table->getPrimaryKeys(keyCondition);
        std::set<std::string> tableKeySet{tableKeyList.begin(), tableKeyList.end()};
        tableKeySet.insert(eqKeyList.begin(), eqKeyList.end());
        auto entries = std::make_shared<precompiled::Entries>();
        for (auto& key : tableKeySet)
        {
            auto entry = table->getRow(key);
            if (condition->filter(entry))
            {
                entries->emplace_back(entry);
            }
        }
        // update the memory gas and the computation gas
        _gasPricer->updateMemUsed(getEntriesCapacity(entries));
        _gasPricer->appendOperation(InterfaceOpcode::Select, entries->size());
        Json::Value records = Json::Value(Json::arrayValue);
        if (entries)
        {
            for (auto& entry : *entries)
            {
                Json::Value record;
                for (auto iter = entry->begin(); iter != entry->end(); iter++)
                {
                    record[iter->first] = iter->second;
                }
                records.append(record);
            }
        }
        auto str = records.toStyledString();
        _callResult->setExecResult(codec->encode(str));
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("CRUDPrecompiled") << LOG_DESC("table open error")
                               << LOG_KV("tableName", tableName);
        getErrorCodeOut(_callResult->mutableExecResult(), CODE_TABLE_NOT_EXIST, codec);
    }
}

int CRUDPrecompiled::parseCondition(const std::string& conditionStr,
    precompiled::Condition::Ptr& condition, std::shared_ptr<PrecompiledGas> _gasPricer)
{
    Json::Reader reader;
    Json::Value conditionJson;
    if (!reader.parse(conditionStr, conditionJson))
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("CRUDPrecompiled")
                               << LOG_DESC("condition json parse error")
                               << LOG_KV("condition", conditionStr);

        return CODE_PARSE_CONDITION_ERROR;
    }
    else
    {
        auto members = conditionJson.getMemberNames();
        Json::Value OPJson;
        for (auto iter = members.begin(); iter != members.end(); iter++)
        {
            if (!isHashField(*iter))
            {
                continue;
            }
            OPJson = conditionJson[*iter];
            auto op = OPJson.getMemberNames();
            for (auto it = op.begin(); it != op.end(); it++)
            {
                if (*it == "eq")
                {
                    condition->EQ(*iter, OPJson[*it].asString());
                    _gasPricer->appendOperation(InterfaceOpcode::EQ);
                }
                else if (*it == "ne")
                {
                    condition->NE(*iter, OPJson[*it].asString());
                    _gasPricer->appendOperation(InterfaceOpcode::NE);
                }
                else if (*it == "gt")
                {
                    condition->GT(*iter, OPJson[*it].asString());
                    _gasPricer->appendOperation(InterfaceOpcode::GT);
                }
                else if (*it == "ge")
                {
                    condition->GE(*iter, OPJson[*it].asString());
                    _gasPricer->appendOperation(InterfaceOpcode::GE);
                }
                else if (*it == "lt")
                {
                    condition->LT(*iter, OPJson[*it].asString());
                    _gasPricer->appendOperation(InterfaceOpcode::LT);
                }
                else if (*it == "le")
                {
                    condition->LE(*iter, OPJson[*it].asString());
                    _gasPricer->appendOperation(InterfaceOpcode::LE);
                }
                else if (*it == "limit")
                {
                    std::string offsetCount = OPJson[*it].asString();
                    std::vector<std::string> offsetCountList;
                    boost::split(offsetCountList, offsetCount, boost::is_any_of(","));
                    int offset = boost::lexical_cast<int>(offsetCountList[0]);
                    int count = boost::lexical_cast<int>(offsetCountList[1]);
                    condition->limit(offset, count);
                    _gasPricer->appendOperation(InterfaceOpcode::Limit);
                }
                else
                {
                    PRECOMPILED_LOG(ERROR)
                        << LOG_BADGE("CRUDPrecompiled") << LOG_DESC("condition operation undefined")
                        << LOG_KV("operation", *it);

                    return CODE_CONDITION_OPERATION_UNDEFINED;
                }
            }
        }
    }
    return CODE_SUCCESS;
}

int CRUDPrecompiled::parseEntry(const std::string& entryStr, Entry::Ptr& entry)
{
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("CRUDPrecompiled") << LOG_DESC("table records")
                           << LOG_KV("entryStr", entryStr);
    Json::Value entryJson;
    Json::Reader reader;
    if (!reader.parse(entryStr, entryJson))
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("CRUDPrecompiled") << LOG_DESC("entry json parse error")
                               << LOG_KV("entry", entryStr);

        return CODE_PARSE_ENTRY_ERROR;
    }
    else
    {
        auto members = entryJson.getMemberNames();
        for (auto& member : members)
        {
            entry->setField(member, entryJson[member].asString());
        }

        return CODE_SUCCESS;
    }
}