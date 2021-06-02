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
 * @file KVTableFactoryPrecompiled.cpp
 * @author: kyonRay
 * @date 2021-05-27
 */

#include "KVTableFactoryPrecompiled.h"
#include "KVTablePrecompiled.h"
#include "Common.h"
#include "Utilities.h"
#include <bcos-framework/interfaces/protocol/Exceptions.h>
#include <bcos-framework/libcodec/abi/ContractABICodec.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/throw_exception.hpp>


using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::precompiled;
using namespace bcos::protocol;

const char* const KV_TABLE_FACTORY_METHOD_OPEN_TABLE = "openTable(string)";
const char* const KV_TABLE_FACTORY_METHOD_CREATE_TABLE = "createTable(string,string,string)";

KVTableFactoryPrecompiled::KVTableFactoryPrecompiled()
{
    name2Selector[KV_TABLE_FACTORY_METHOD_OPEN_TABLE] =
        getFuncSelector(KV_TABLE_FACTORY_METHOD_OPEN_TABLE);
    name2Selector[KV_TABLE_FACTORY_METHOD_CREATE_TABLE] =
        getFuncSelector(KV_TABLE_FACTORY_METHOD_CREATE_TABLE);
}

std::string KVTableFactoryPrecompiled::toString()
{
    return "KVTableFactory";
}

PrecompiledExecResult::Ptr KVTableFactoryPrecompiled::call(
    std::shared_ptr<executor::ExecutiveContext> _context, bytesConstRef _param,
    const std::string& _origin, const std::string& _sender, u256& _remainGas)
{
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("KVTableFactory") << LOG_DESC("call")
                           << LOG_KV("func", func);

    codec::abi::ContractABICodec abi(nullptr);
    auto callResult = std::make_shared<PrecompiledExecResult>();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();
    gasPricer->setMemUsed(_param.size());

    if (func == name2Selector[KV_TABLE_FACTORY_METHOD_OPEN_TABLE])
    {  // openTable(string)
        std::string tableName;
        abi.abiOut(data, tableName);
        tableName = precompiled::getTableName(tableName);
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("KVTableFactory") << LOG_KV("openTable", tableName);
        Address address;
        auto table = m_memoryTableFactory->openTable(tableName);
        gasPricer->appendOperation(InterfaceOpcode::OpenTable);
        if (table)
        {
            auto kvTablePrecompiled = std::make_shared<KVTablePrecompiled>();
            kvTablePrecompiled->setTable(table);
            address = _context->registerPrecompiled(kvTablePrecompiled);
        }
        else
        {
            PRECOMPILED_LOG(WARNING)
                << LOG_BADGE("KVTableFactoryPrecompiled") << LOG_DESC("Open new table failed")
                << LOG_KV("table name", tableName);
            BOOST_THROW_EXCEPTION(PrecompiledError() <<errinfo_comment(tableName + " does not exist"));
        }
        callResult->setExecResult(abi.abiIn("", address));
    }
    else if (func == name2Selector[KV_TABLE_FACTORY_METHOD_CREATE_TABLE])
    {  // createTable(string,string,string)
        if (!checkAuthority(_context->getTableFactory(), _origin, _sender))
        {
            PRECOMPILED_LOG(ERROR)
                << LOG_BADGE("KVTableFactoryPrecompiled") << LOG_DESC("permission denied")
                << LOG_KV("origin", _origin) << LOG_KV("contract", _sender);
            BOOST_THROW_EXCEPTION(
                PrecompiledError() << errinfo_comment("Permission denied. " + _origin + " can't call contract " + _sender));
        }
        std::string tableName;
        std::string keyField;
        std::string valueFiled;

        abi.abiOut(data, tableName, keyField, valueFiled);
        PRECOMPILED_LOG(INFO) << LOG_BADGE("KVTableFactory") << LOG_KV("createTable", tableName)
                              << LOG_KV("keyField", keyField) << LOG_KV("valueFiled", valueFiled);

        std::vector<std::string> fieldNameList;
        boost::split(fieldNameList, valueFiled, boost::is_any_of(","));
        boost::trim(keyField);
        if (keyField.size() > (size_t)SYS_TABLE_KEY_FIELD_NAME_MAX_LENGTH)
        {  // mysql TableName and fieldName length limit is 64
            BOOST_THROW_EXCEPTION(PrecompiledError() << errinfo_comment(
                                      std::string("table field name length overflow ") +
                                      std::to_string(SYS_TABLE_KEY_FIELD_NAME_MAX_LENGTH)));
        }
        for (auto& str : fieldNameList)
        {
            boost::trim(str);
            if (str.size() > (size_t)SYS_TABLE_KEY_FIELD_NAME_MAX_LENGTH)
            {  // mysql TableName and fieldName length limit is 64
                BOOST_THROW_EXCEPTION(PrecompiledError() << errinfo_comment(
                                          std::string("table field name length overflow ") +
                                          std::to_string(SYS_TABLE_KEY_FIELD_NAME_MAX_LENGTH)));
            }
        }

        std::vector<std::string> keyFieldList{keyField};
        checkNameValidate(tableName, keyFieldList, fieldNameList);

        valueFiled = boost::join(fieldNameList, ",");
        if (valueFiled.size() > (size_t)SYS_TABLE_VALUE_FIELD_MAX_LENGTH)
        {
            BOOST_THROW_EXCEPTION(PrecompiledError() << errinfo_comment(
                                      std::string("total table field name length overflow ") +
                                      std::to_string(SYS_TABLE_VALUE_FIELD_MAX_LENGTH)));
        }

        tableName = precompiled::getTableName(tableName);
        if (tableName.size() > (size_t)USER_TABLE_NAME_MAX_LENGTH_S)
        {  // mysql TableName and fieldName length limit is 64
            // 2.2.0 user tableName length limit is 50-2=48
            BOOST_THROW_EXCEPTION(
                PrecompiledError() << errinfo_comment(std::string("tableName length overflow ") +
                                                      std::to_string(USER_TABLE_NAME_MAX_LENGTH)));
        }

        int result = 0;
        auto createResult = m_memoryTableFactory->createTable(tableName, keyField, valueFiled);
        if (!createResult)
        {
            // table already exist
            result = CODE_TABLE_NAME_ALREADY_EXIST;
        }
        else
        {
            gasPricer->appendOperation(InterfaceOpcode::CreateTable);
        }
        getErrorCodeOut(callResult->mutableExecResult(), result);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("KVTableFactoryPrecompiled")
                               << LOG_DESC("call undefined function!");
    }
    gasPricer->updateMemUsed(callResult->m_execResult.size());
    _remainGas -= gasPricer->calTotalGas();
    return callResult;
}

h256 KVTableFactoryPrecompiled::hash()
{
    return m_memoryTableFactory->hash();
}
