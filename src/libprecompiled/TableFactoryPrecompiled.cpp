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
 * @file TableFactoryPrecompiled.cpp
 * @author: kyonRay
 * @date 2021-05-30
 */

#include "TableFactoryPrecompiled.h"
#include "Common.h"
#include "PrecompiledResult.h"
#include "TablePrecompiled.h"
#include "Utilities.h"
#include <bcos-framework/interfaces/protocol/Exceptions.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/throw_exception.hpp>


using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::precompiled;
using namespace bcos::protocol;

const char* const TABLE_METHOD_OPT_STR = "openTable(string)";
const char* const TABLE_METHOD_CRT_STR_STR = "createTable(string,string,string)";

TableFactoryPrecompiled::TableFactoryPrecompiled(crypto::Hash::Ptr _hashImpl)
  : Precompiled(_hashImpl)
{
    name2Selector[TABLE_METHOD_OPT_STR] = getFuncSelector(TABLE_METHOD_OPT_STR, _hashImpl);
    name2Selector[TABLE_METHOD_CRT_STR_STR] = getFuncSelector(TABLE_METHOD_CRT_STR_STR, _hashImpl);
}

std::string TableFactoryPrecompiled::toString()
{
    return "TableFactory";
}

PrecompiledExecResult::Ptr TableFactoryPrecompiled::call(
    std::shared_ptr<executor::BlockContext> _context, bytesConstRef _param,
    const std::string& _origin, const std::string& _sender, u256& _remainGas)
{
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);
    auto callResult = std::make_shared<PrecompiledExecResult>();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();
    gasPricer->setMemUsed(_param.size());

    if (func == name2Selector[TABLE_METHOD_OPT_STR])
    {
        // openTable(string)
        openTable(_context, data, callResult, gasPricer);
    }
    else if (func == name2Selector[TABLE_METHOD_CRT_STR_STR])
    {
        // createTable(string,string,string)
        createTable(_context, data, callResult, _origin, _sender, gasPricer);
    }
    else
    {
        STORAGE_LOG(ERROR) << LOG_BADGE("TableFactoryPrecompiled")
                           << LOG_DESC("call undefined function!");
    }
    gasPricer->updateMemUsed(callResult->m_execResult.size());
    _remainGas -= gasPricer->calTotalGas();
    return callResult;
}

crypto::HashType TableFactoryPrecompiled::hash()
{
    return m_memoryTableFactory->hash();
}

void TableFactoryPrecompiled::checkCreateTableParam(
    const std::string& _tableName, std::string& _keyField, std::string& _valueField)
{
    std::vector<std::string> keyNameList;
    boost::split(keyNameList, _keyField, boost::is_any_of(","));
    std::vector<std::string> fieldNameList;
    boost::split(fieldNameList, _valueField, boost::is_any_of(","));

    if (_keyField.size() > (size_t)SYS_TABLE_KEY_FIELD_NAME_MAX_LENGTH)
    {  // mysql TableName and fieldName length limit is 64
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError() << errinfo_comment(
                                  "table field name length overflow " +
                                  std::to_string(SYS_TABLE_KEY_FIELD_NAME_MAX_LENGTH)));
    }
    for (auto& str : keyNameList)
    {
        boost::trim(str);
        if (str.size() > (size_t)SYS_TABLE_KEY_FIELD_NAME_MAX_LENGTH)
        {  // mysql TableName and fieldName length limit is 64
            BOOST_THROW_EXCEPTION(
                protocol::PrecompiledError()
                << errinfo_comment("errorCode" + std::to_string(CODE_TABLE_FIELD_LENGTH_OVERFLOW))
                << errinfo_comment(std::string("table key name length overflow ") +
                                   std::to_string(SYS_TABLE_KEY_FIELD_NAME_MAX_LENGTH)));
        }
    }

    for (auto& str : fieldNameList)
    {
        boost::trim(str);
        if (str.size() > (size_t)SYS_TABLE_KEY_FIELD_NAME_MAX_LENGTH)
        {  // mysql TableName and fieldName length limit is 64
            BOOST_THROW_EXCEPTION(
                protocol::PrecompiledError()
                << errinfo_comment("errorCode" + std::to_string(CODE_TABLE_FIELD_LENGTH_OVERFLOW))
                << errinfo_comment(std::string("table field name length overflow ") +
                                   std::to_string(SYS_TABLE_KEY_FIELD_NAME_MAX_LENGTH)));
        }
    }

    checkNameValidate(_tableName, keyNameList, fieldNameList);

    _keyField = boost::join(keyNameList, ",");
    _valueField = boost::join(fieldNameList, ",");
    if (_keyField.size() > (size_t)SYS_TABLE_KEY_FIELD_MAX_LENGTH)
    {
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError() << errinfo_comment(
                                  std::string("total table key name length overflow ") +
                                  std::to_string(SYS_TABLE_KEY_FIELD_MAX_LENGTH)));
    }
    if (_valueField.size() > (size_t)SYS_TABLE_VALUE_FIELD_MAX_LENGTH)
    {
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError() << errinfo_comment(
                                  std::string("total table field name length overflow ") +
                                  std::to_string(SYS_TABLE_VALUE_FIELD_MAX_LENGTH)));
    }

    auto tableName = precompiled::getTableName(_tableName, true);
    if (tableName.size() > (size_t)USER_TABLE_NAME_MAX_LENGTH ||
        (tableName.size() > (size_t)USER_TABLE_NAME_MAX_LENGTH_S))
    {
        // mysql TableName and fieldName length limit is 64
        BOOST_THROW_EXCEPTION(
            protocol::PrecompiledError()
            << errinfo_comment("errorCode: " + std::to_string(CODE_TABLE_NAME_LENGTH_OVERFLOW))
            << errinfo_comment(std::string("tableName length overflow ") +
                               std::to_string(USER_TABLE_NAME_MAX_LENGTH)));
    }
}

void TableFactoryPrecompiled::openTable(const std::shared_ptr<executor::BlockContext>& _context,
    bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
    const PrecompiledGas::Ptr& gasPricer)
{
    // openTable(string)
    std::string tableName;
    auto codec = std::make_shared<PrecompiledCodec>(_context->hashHandler(), _context->isWasm());
    codec->decode(data, tableName);
    tableName = getTableName(tableName, _context->isWasm());
    auto table = m_memoryTableFactory->openTable(tableName);
    gasPricer->appendOperation(InterfaceOpcode::OpenTable);
    if (!table)
    {
        PRECOMPILED_LOG(WARNING) << LOG_BADGE("TableFactoryPrecompiled")
                                 << LOG_DESC("Open new table failed")
                                 << LOG_KV("table name", tableName);
        BOOST_THROW_EXCEPTION(PrecompiledError() << errinfo_comment(tableName + " does not exist"));
    }
    auto tablePrecompiled = std::make_shared<TablePrecompiled>(m_hashImpl);
    tablePrecompiled->setTable(table);
    if (_context->isWasm())
    {
        auto address = _context->registerPrecompiled(tablePrecompiled);
        callResult->setExecResult(codec->encode(address));
    }
    else
    {
        auto address = Address(_context->registerPrecompiled(tablePrecompiled));
        callResult->setExecResult(codec->encode(address));
    }
}

void TableFactoryPrecompiled::createTable(const std::shared_ptr<executor::BlockContext>& _context,
    bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
    const std::string& _origin, const std::string& _sender, const PrecompiledGas::Ptr& gasPricer)
{
    // createTable(string,string,string)
    if (!checkAuthority(_context->getTableFactory(), _origin, _sender))
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TableFactoryPrecompiled")
                               << LOG_DESC("permission denied") << LOG_KV("origin", _origin)
                               << LOG_KV("contract", _sender);
        BOOST_THROW_EXCEPTION(
            protocol::PrecompiledError() << errinfo_comment(
                "Permission denied. " + _origin + " can't call contract " + _sender));
    }
    std::string tableName;
    std::string keyField;
    std::string valueField;
    auto codec = std::make_shared<PrecompiledCodec>(_context->hashHandler(), _context->isWasm());
    codec->decode(data, tableName, keyField, valueField);

    if (_context->isWasm() && (tableName.empty() || tableName.at(0) != '/'))
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TableFactoryPrecompiled")
                               << LOG_DESC("create table in wasm env: error tableName")
                               << LOG_KV("tableName", tableName);
        BOOST_THROW_EXCEPTION(PrecompiledError() << errinfo_comment("Table name error."));
    }

    checkCreateTableParam(tableName, keyField, valueField);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TableFactory") << LOG_KV("createTable", tableName)
                           << LOG_KV("keyField", keyField) << LOG_KV("valueFiled", valueField);
    // FIXME: use /group/tables
    // wasm: /tables + tableName, evm: u_ + tableName
    auto newTableName = getTableName(tableName, _context->isWasm());
    int result = CODE_SUCCESS;
    auto table = m_memoryTableFactory->openTable(newTableName);
    if (table)
    {
        // table already exist
        result = CODE_TABLE_NAME_ALREADY_EXIST;
        getErrorCodeOut(callResult->mutableExecResult(), result, codec);
        return;
    }
    else
    {
        if (_context->isWasm())
        {
            auto parentDir = getParentDir(newTableName);
            auto tableRelativePath = getDirBaseName(newTableName);
            if (!recursiveBuildDir(m_memoryTableFactory, newTableName))
            {
                result = CODE_FILE_BUILD_DIR_FAILED;
            }
            else
            {
                m_memoryTableFactory->createTable(newTableName, keyField, valueField);
                gasPricer->appendOperation(InterfaceOpcode::CreateTable);

                // parentPath table must exist
                // update parentDir
                auto parentTable = m_memoryTableFactory->openTable(parentDir);
                auto newEntry = parentTable->newEntry();
                newEntry->setField(FS_FIELD_TYPE, FS_TYPE_CONTRACT);
                // FIXME: consider permissions inheritance
                newEntry->setField(FS_FIELD_ACCESS, "");
                newEntry->setField(FS_FIELD_OWNER, _origin);
                newEntry->setField(FS_FIELD_GID, "");
                newEntry->setField(FS_FIELD_EXTRA, "");
                parentTable->setRow(tableRelativePath, newEntry);
            }
        }
        else
        {
            m_memoryTableFactory->createTable(newTableName, keyField, valueField);
            gasPricer->appendOperation(InterfaceOpcode::CreateTable);
        }
    }
    getErrorCodeOut(callResult->mutableExecResult(), result, codec);
}