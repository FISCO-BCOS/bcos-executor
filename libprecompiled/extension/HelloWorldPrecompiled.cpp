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
 * @file HelloWorldPrecompiled.cpp
 * @author: kyonRay
 * @date 2021-05-30
 */

#include "HelloWorldPrecompiled.h"
#include "HelloWorldPrecompiled.h"
#include "../../libexecutor/ExecutiveContext.h"
#include "../Utilities.h"
#include <bcos-framework/libcodec/abi/ContractABICodec.h>

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::precompiled;

/*
contract HelloWorld {
    function get() public constant returns(string);
    function set(string _m) public;
}
*/

// HelloWorldPrecompiled table name
const std::string HELLO_WORLD_TABLE_NAME = "hello_world";
// key field
const std::string HELLO_WORLD_KEY_FIELD = "key";
const std::string HELLO_WORLD_KEY_FIELD_NAME = "hello_key";
// value field
const std::string HELLO_WORLD_VALUE_FIELD = "value";

// get interface
const char* const HELLO_WORLD_METHOD_GET = "get()";
// set interface
const char* const HELLO_WORLD_METHOD_SET = "set(string)";

HelloWorldPrecompiled::HelloWorldPrecompiled()
{
    name2Selector[HELLO_WORLD_METHOD_GET] = getFuncSelector(HELLO_WORLD_METHOD_GET);
    name2Selector[HELLO_WORLD_METHOD_SET] = getFuncSelector(HELLO_WORLD_METHOD_SET);
}

std::string HelloWorldPrecompiled::toString()
{
    return "HelloWorld";
}

PrecompiledExecResult::Ptr HelloWorldPrecompiled::call(
    std::shared_ptr<executor::ExecutiveContext> _context, bytesConstRef _param,
    const std::string& _origin, const std::string&, u256& _remainGas)
{
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("HelloWorldPrecompiled") << LOG_DESC("call")
                           << LOG_KV("param", toHexString(_param));

    // parse function name
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);
    auto callResult = m_precompiledExecResultFactory->createPrecompiledResult();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();
    gasPricer->setMemUsed(_param.size());

    codec::abi::ContractABICodec abi(nullptr);
    auto table =
        _context->getTableFactory()->openTable(precompiled::getTableName(HELLO_WORLD_TABLE_NAME));
    gasPricer->appendOperation(InterfaceOpcode::OpenTable);
    if (!table)
    {
        // table is not exist, create it.
        table = createTable(_context->getTableFactory(),
            precompiled::getTableName(HELLO_WORLD_TABLE_NAME), HELLO_WORLD_KEY_FIELD,
            HELLO_WORLD_VALUE_FIELD);
        gasPricer->appendOperation(InterfaceOpcode::CreateTable);
        if (!table)
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("HelloWorldPrecompiled") << LOG_DESC("set")
                                   << LOG_DESC("open table failed.");
            getErrorCodeOut(callResult->mutableExecResult(), CODE_NO_AUTHORIZED);
            return callResult;
        }
    }
    if (func == name2Selector[HELLO_WORLD_METHOD_GET])
    {  // get() function call
        // default retMsg
        std::string retValue = "Hello World!";

        auto entry = table->getRow(HELLO_WORLD_KEY_FIELD_NAME);
        if (!entry)
        {

            gasPricer->updateMemUsed(entry->capacityOfHashField());
            gasPricer->appendOperation(InterfaceOpcode::Select, 1);

            retValue = entry->getField(HELLO_WORLD_VALUE_FIELD);
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("HelloWorldPrecompiled") << LOG_DESC("get")
                                   << LOG_KV("value", retValue);
        }
        callResult->setExecResult(abi.abiIn("", retValue));
    }
    else if (func == name2Selector[HELLO_WORLD_METHOD_SET])
    {  // set(string) function call

        std::string strValue;
        abi.abiOut(data, strValue);
        auto entry = table->getRow(HELLO_WORLD_KEY_FIELD_NAME);
        gasPricer->updateMemUsed(entry->capacityOfHashField());
        gasPricer->appendOperation(InterfaceOpcode::Select, 1);
        entry->setField(HELLO_WORLD_VALUE_FIELD, strValue);

        size_t count = 0;
        if(!_context->getTableFactory()->checkAuthority(HELLO_WORLD_TABLE_NAME, _origin))
        {
            PRECOMPILED_LOG(ERROR)
                    << LOG_BADGE("HelloWorldPrecompiled") << LOG_DESC(" permission denied ")
                    << LOG_KV("origin", _origin) << LOG_KV("func", func);
        }
        table->setRow(HELLO_WORLD_KEY_FIELD_NAME, entry);
        auto commitResult = _context->getTableFactory()->commit();
        count = commitResult.first;
        if (!commitResult.second ||
            commitResult.second->errorCode() == protocol::CommonError::SUCCESS)
        {
            gasPricer->appendOperation(InterfaceOpcode::Update, count);
            gasPricer->updateMemUsed(entry->capacityOfHashField() * count);
        }
        else
        {
            PRECOMPILED_LOG(ERROR)
                << LOG_BADGE("HelloWorldPrecompiled") << LOG_DESC(" commit error ")
                << LOG_KV("errorCode", commitResult.second->errorCode()) << LOG_KV("func", func);
        }

        getErrorCodeOut(callResult->mutableExecResult(), count);
    }
    else
    {  // unknown function call
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("HelloWorldPrecompiled") << LOG_DESC(" unknown function ")
                               << LOG_KV("func", func);
        callResult->setExecResult(abi.abiIn("", u256((int)CODE_UNKNOW_FUNCTION_CALL)));
    }
    gasPricer->updateMemUsed(callResult->m_execResult.size());
    _remainGas -= gasPricer->calTotalGas();
    return callResult;
}
