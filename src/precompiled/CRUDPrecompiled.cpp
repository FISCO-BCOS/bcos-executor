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
#include <bcos-framework/interfaces/storage/StorageInterface.h>
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
    std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _param,
    const std::string&, const std::string&)
{
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);

    auto callResult = std::make_shared<PrecompiledExecResult>();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();
    gasPricer->setMemUsed(_param.size());
    if (func == name2Selector[CRUD_METHOD_DESC_STR])
    {  // desc(string)
        desc(_executive, data, callResult, gasPricer);
    }
    else if (func == name2Selector[CRUD_METHOD_INSERT_STR])
    {
        // insert(string tableName, string entry, string optional)
        insert(_executive, data, callResult, gasPricer);
    }
    else if (func == name2Selector[CRUD_METHOD_UPDATE_STR])
    {
        // update(string tableName, string entry, string condition, string optional)
        update(_executive, data, callResult, gasPricer);
    }
    else if (func == name2Selector[CRUD_METHOD_REMOVE_STR])
    {
        // remove(string tableName, string condition, string optional)
        remove(_executive, data, callResult, gasPricer);
    }
    else if (func == name2Selector[CRUD_METHOD_SELECT_STR])
    {
        select(_executive, data, callResult, gasPricer);
    }
    else
    {
        codec::abi::ContractABICodec abi(nullptr);
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("CRUDPrecompiled")
                               << LOG_DESC("call undefined function") << LOG_KV("func", func);
        callResult->setExecResult(abi.abiIn("", u256((int)CODE_UNKNOW_FUNCTION_CALL)));
    }
    gasPricer->updateMemUsed(callResult->m_execResult.size());
    callResult->setGas(gasPricer->calTotalGas());
    return callResult;
}

void CRUDPrecompiled::desc(const std::shared_ptr<executor::TransactionExecutive>& _executive,
    bytesConstRef _paramData, const PrecompiledExecResult::Ptr& _callResult,
    const std::shared_ptr<PrecompiledGas>& _gasPricer)
{
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    std::string tableName;
    codec->decode(_paramData, tableName);
    tableName = precompiled::getTableName(tableName);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("CRUDPrecompiled") << LOG_KV("desc", tableName);

    // s_tables must exist
    auto table = _executive->storage().openTable(StorageInterface::SYS_TABLES);
    _gasPricer->appendOperation(InterfaceOpcode::OpenTable);

    auto entry = table->getRow(tableName);
    std::string keyField, valueField;
    if (entry)
    {
        _gasPricer->appendOperation(InterfaceOpcode::Select, entry->capacityOfHashField());
        auto valueKey = entry->getField(StorageInterface::SYS_TABLE_VALUE_FIELDS);
        keyField = valueKey.substr(valueKey.find_last_of(',') + 1);
        valueField = valueKey.substr(0, valueKey.find_last_of(','));
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("CRUDPrecompiled") << LOG_BADGE("DESC")
                               << LOG_DESC("table not exist") << LOG_KV("tableName", tableName);
    }
    _callResult->setExecResult(codec->encode(keyField, valueField));
}

void CRUDPrecompiled::update(const std::shared_ptr<executor::TransactionExecutive>& _executive,
    bytesConstRef _paramData, const PrecompiledExecResult::Ptr& _callResult,
    const std::shared_ptr<PrecompiledGas>& _gasPricer)
{
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    std::string tableName, entryStr, conditionStr, optional;
    codec->decode(_paramData, tableName, entryStr, conditionStr, optional);
    tableName = precompiled::getTableName(tableName);

    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("CRUDPrecompiled") << LOG_KV("update", tableName);

    auto table = _executive->storage().openTable(tableName);
    _gasPricer->appendOperation(InterfaceOpcode::OpenTable);
    // get key field name from s_tables
    auto sysTable = _executive->storage().openTable(StorageInterface::SYS_TABLES);
    auto sysEntry = sysTable->getRow(tableName);
    if (table && sysEntry)
    {
        auto valueKeyCombined = sysEntry->getField(StorageInterface::SYS_TABLE_VALUE_FIELDS);
        auto keyField =
            std::string(valueKeyCombined.substr(valueKeyCombined.find_last_of(',') + 1));
        std::string keyValue;
        auto entry = table->newEntry();
        int parseEntryResult = parseEntry(entryStr, entry, keyField, keyValue);
        if (parseEntryResult != CODE_SUCCESS)
        {
            getErrorCodeOut(_callResult->mutableExecResult(), parseEntryResult, codec);
            return;
        }
        // check the entry value valid
        for (auto entryValue : entry)
        {
            checkLengthValidate(entryValue, USER_TABLE_FIELD_VALUE_MAX_LENGTH,
                CODE_TABLE_KEY_VALUE_LENGTH_OVERFLOW);
        }
        if (!keyValue.empty())
        {
            // key in update entry
            getErrorCodeOut(_callResult->mutableExecResult(), CODE_INVALID_UPDATE_TABLE_KEY, codec);
            return;
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
            if (cond.left == keyField)
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
                                   << LOG_KV("primaryKey", keyField);
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
                    << LOG_KV("primaryKey", keyField) << LOG_KV("notExistKey", key);
                eqKeyExist = false;
                getErrorCodeOut(_callResult->mutableExecResult(), CODE_UPDATE_KEY_NOT_EXIST, codec);
                break;
            }
        }
        if (eqKeyExist)
        {
            auto tableKeyList = table->getPrimaryKeys(*keyCondition);
            std::set<std::string> tableKeySet{tableKeyList.begin(), tableKeyList.end()};
            tableKeySet.insert(eqKeyList.begin(), eqKeyList.end());
            auto updateCount = 0;
            for (auto& tableKey : tableKeySet)
            {
                auto tableEntry = table->getRow(tableKey);
                if (condition->filter(tableEntry))
                {
                    for (auto const& field : entry.tableInfo()->fields())
                    {
                        auto value = entry.getField(field);
                        if (value.empty())
                            continue;
                        tableEntry->setField(field, std::string(value));
                    }
                    table->setRow(tableKey, std::move(tableEntry.value()));
                    updateCount++;
                }
            }
            _gasPricer->setMemUsed(entry.capacityOfHashField());
            _gasPricer->appendOperation(InterfaceOpcode::Update, updateCount);
            getErrorCodeOut(_callResult->mutableExecResult(), updateCount, codec);
        }
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("CRUDPrecompiled") << LOG_BADGE("UPDATE")
                               << LOG_DESC("table open error") << LOG_KV("tableName", tableName);
        getErrorCodeOut(_callResult->mutableExecResult(), CODE_TABLE_NOT_EXIST, codec);
    }
}

void CRUDPrecompiled::insert(const std::shared_ptr<executor::TransactionExecutive>& _executive,
    bytesConstRef _paramData, const PrecompiledExecResult::Ptr& _callResult,
    const std::shared_ptr<PrecompiledGas>& _gasPricer)
{
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    std::string tableName, entryStr, optional;
    codec->decode(_paramData, tableName, entryStr, optional);
    tableName = precompiled::getTableName(tableName);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("CRUDPrecompiled") << LOG_KV("insert", tableName);

    auto table = _executive->storage().openTable(tableName);
    _gasPricer->appendOperation(InterfaceOpcode::OpenTable);

    // get key field name from s_tables
    auto keyField = getKeyField(_executive, tableName);
    if (table && !keyField.empty())
    {
        std::string keyValue;
        auto tableInfo = table->tableInfo();
        auto entry = table->newEntry();
        int parseEntryResult = parseEntry(entryStr, entry, std::string(keyField), keyValue);
        if (parseEntryResult != CODE_SUCCESS)
        {
            getErrorCodeOut(_callResult->mutableExecResult(), parseEntryResult, codec);
            return;
        }
        // check entry
        for (auto entryValue : entry)
        {
            checkLengthValidate(entryValue, USER_TABLE_FIELD_VALUE_MAX_LENGTH,
                CODE_TABLE_KEY_VALUE_LENGTH_OVERFLOW);
        }
        if (keyValue.empty())
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("CRUDPrecompiled") << LOG_BADGE("INSERT")
                                   << LOG_DESC("can't find specific key in entry")
                                   << LOG_KV("table", tableName) << LOG_KV("key", keyField);
            getErrorCodeOut(_callResult->mutableExecResult(), CODE_KEY_NOT_EXIST_IN_ENTRY, codec);
            return;
        }
        if (table->getRow(keyValue))
        {
            PRECOMPILED_LOG(ERROR)
                << LOG_BADGE("CRUDPrecompiled") << LOG_BADGE("INSERT")
                << LOG_DESC("key already exist in table, please use UPDATE method")
                << LOG_KV("primaryKey", keyField) << LOG_KV("existKey", keyValue);
            getErrorCodeOut(_callResult->mutableExecResult(), CODE_INSERT_KEY_EXIST, codec);
            return;
        }
        auto capacityOfHashField = entry.capacityOfHashField();
        table->setRow(keyValue, std::move(entry));
        _gasPricer->appendOperation(InterfaceOpcode::Insert, 1);
        _gasPricer->updateMemUsed(capacityOfHashField);
        _callResult->setExecResult(codec->encode(u256(1)));
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("CRUDPrecompiled") << LOG_DESC("table open error")
                               << LOG_KV("tableName", tableName);
        getErrorCodeOut(_callResult->mutableExecResult(), CODE_TABLE_NOT_EXIST, codec);
    }
}

void CRUDPrecompiled::remove(const std::shared_ptr<executor::TransactionExecutive>& _executive,
    bytesConstRef _paramData, const PrecompiledExecResult::Ptr& _callResult,
    const std::shared_ptr<PrecompiledGas>& _gasPricer)
{
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    std::string tableName, conditionStr, optional;
    codec->decode(_paramData, tableName, conditionStr, optional);
    tableName = precompiled::getTableName(tableName);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("CRUDPrecompiled") << LOG_KV("remove", tableName);

    auto table = _executive->storage().openTable(tableName);
    _gasPricer->appendOperation(InterfaceOpcode::OpenTable);
    auto keyField = getKeyField(_executive, tableName);
    if (table && !keyField.empty())
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
            if (cond.left == keyField)
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
                                   << LOG_KV("primaryKey", keyField);
            getErrorCodeOut(_callResult->mutableExecResult(), CODE_KEY_NOT_EXIST_IN_COND, codec);
            return;
        }
        auto tableKeyList = table->getPrimaryKeys(*keyCondition);
        std::set<std::string> tableKeySet{tableKeyList.begin(), tableKeyList.end()};
        tableKeySet.insert(eqKeyList.begin(), eqKeyList.end());
        auto rmCount = 0;
        for (auto& tableKey : tableKeySet)
        {
            auto entry = table->getRow(tableKey);
            if (condition->filter(entry))
            {
                table->setRow(tableKey, table->newDeletedEntry());
                rmCount++;
            }
        }
        _gasPricer->appendOperation(InterfaceOpcode::Remove, rmCount);
        _gasPricer->updateMemUsed(rmCount);
        getErrorCodeOut(_callResult->mutableExecResult(), rmCount, codec);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("CRUDPrecompiled") << LOG_DESC("table open error")
                               << LOG_KV("tableName", tableName);
        getErrorCodeOut(_callResult->mutableExecResult(), CODE_TABLE_NOT_EXIST, codec);
    }
}

void CRUDPrecompiled::select(const std::shared_ptr<executor::TransactionExecutive>& _executive,
    bytesConstRef _paramData, const PrecompiledExecResult::Ptr& _callResult,
    const std::shared_ptr<PrecompiledGas>& _gasPricer)
{
    // select(string tableName, string condition, string optional)
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    std::string tableName, conditionStr, optional;
    codec->decode(_paramData, tableName, conditionStr, optional);
    if (tableName != StorageInterface::SYS_TABLES)
    {
        tableName = precompiled::getTableName(tableName);
    }
    auto table = _executive->storage().openTable(tableName);
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();
    gasPricer->appendOperation(InterfaceOpcode::OpenTable);
    auto keyField = getKeyField(_executive, tableName);
    if (table && !keyField.empty())
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
            if (cond.left == keyField)
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
                                   << LOG_KV("primaryKey", keyField);
            getErrorCodeOut(_callResult->mutableExecResult(), CODE_KEY_NOT_EXIST_IN_COND, codec);
            return;
        }
        // merge keys from storage and eqKeys
        auto tableKeyList = table->getPrimaryKeys(*keyCondition);
        std::set<std::string> tableKeySet{tableKeyList.begin(), tableKeyList.end()};
        tableKeySet.insert(eqKeyList.begin(), eqKeyList.end());
        auto entries = std::make_shared<precompiled::Entries>();
        for (auto& keyValue : tableKeySet)
        {
            auto entry = table->getRow(keyValue);
            if (condition->filter(entry))
            {
                entry->setField(keyField, keyValue);
                entries->emplace_back(std::move(entry.value()));
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
                for (auto& field : entry.tableInfo()->fields())
                {
                    record[field] = std::string(entry.getField(field));
                }
                records.append(record);
            }
        }
        _callResult->setExecResult(codec->encode(records.toStyledString()));
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("CRUDPrecompiled") << LOG_DESC("table open error")
                               << LOG_KV("tableName", tableName);
        getErrorCodeOut(_callResult->mutableExecResult(), CODE_TABLE_NOT_EXIST, codec);
    }
}

int CRUDPrecompiled::parseCondition(const std::string& conditionStr,
    precompiled::Condition::Ptr& condition, const std::shared_ptr<PrecompiledGas>& _gasPricer)
{
    Json::Reader reader;
    Json::Value conditionJson;
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("CRUDPrecompiled") << LOG_DESC("table records")
                           << LOG_KV("conditionStr", conditionStr);
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
        for (auto& member : members)
        {
            if (!isHashField(member))
            {
                continue;
            }
            OPJson = conditionJson[member];
            auto ops = OPJson.getMemberNames();
            for (auto& op : ops)
            {
                if (op == "eq")
                {
                    condition->EQ(member, OPJson[op].asString());
                    _gasPricer->appendOperation(InterfaceOpcode::EQ);
                }
                else if (op == "ne")
                {
                    condition->NE(member, OPJson[op].asString());
                    _gasPricer->appendOperation(InterfaceOpcode::NE);
                }
                else if (op == "gt")
                {
                    condition->GT(member, OPJson[op].asString());
                    _gasPricer->appendOperation(InterfaceOpcode::GT);
                }
                else if (op == "ge")
                {
                    condition->GE(member, OPJson[op].asString());
                    _gasPricer->appendOperation(InterfaceOpcode::GE);
                }
                else if (op == "lt")
                {
                    condition->LT(member, OPJson[op].asString());
                    _gasPricer->appendOperation(InterfaceOpcode::LT);
                }
                else if (op == "le")
                {
                    condition->LE(member, OPJson[op].asString());
                    _gasPricer->appendOperation(InterfaceOpcode::LE);
                }
                else if (op == "limit")
                {
                    std::string offsetCount = OPJson[op].asString();
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
                        << LOG_KV("operation", op);

                    return CODE_CONDITION_OPERATION_UNDEFINED;
                }
            }
        }
    }
    return CODE_SUCCESS;
}

int CRUDPrecompiled::parseEntry(
    const std::string& entryStr, Entry& entry, const std::string& keyField, std::string& keyValue)
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
            if (member == keyField)
            {
                keyValue = entryJson[keyField].asString();
            }
            entry.setField(member, entryJson[member].asString());
        }

        return CODE_SUCCESS;
    }
}