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
 * @file ParallelConfigPrecompiled.cpp
 * @author: kyonRay
 * @date 2021-05-28
 */

#include "ParallelConfigPrecompiled.h"
#include "Utilities.h"
#include "Common.h"
#include <bcos-framework/interfaces/protocol/Exceptions.h>
#include <bcos-framework/interfaces/storage/TableInterface.h>
#include <bcos-framework/libcodec/abi/ContractABICodec.h>
#include <bcos-framework/interfaces/protocol/CommonError.h>
#include <boost/algorithm/string.hpp>

using namespace bcos;
using namespace bcos::storage;
using namespace bcos::executor;
using namespace bcos::codec::abi;
using namespace bcos::precompiled;

/*
    table name: PARA_CONFIG_TABLE_PREFIX_CONTRACT_ADDR_
    | selector   | functionName                    | criticalSize |
    | ---------- | ------------------------------- | ------------ |
    | 0x12345678 | transfer(string,string,uint256) | 2            |
    | 0x23456789 | set(string,uint256)             | 1            |
*/

const std::string PARA_SELECTOR = "selector";
const std::string PARA_FUNC_NAME = "functionName";
const std::string PARA_CRITICAL_SIZE = "criticalSize";

const std::string PARA_CONFIG_REGISTER_METHOD_ADDR_STR_UINT =
    "registerParallelFunctionInternal(string,string,uint256)";
const std::string PARA_CONFIG_UNREGISTER_METHOD_ADDR_STR =
    "unregisterParallelFunctionInternal(string,string)";

const std::string PARA_KEY_NAME = PARA_SELECTOR;
const std::string PARA_VALUE_NAMES = PARA_FUNC_NAME + "," + PARA_CRITICAL_SIZE;


ParallelConfigPrecompiled::ParallelConfigPrecompiled()
{
    name2Selector[PARA_CONFIG_REGISTER_METHOD_ADDR_STR_UINT] =
        getFuncSelector(PARA_CONFIG_REGISTER_METHOD_ADDR_STR_UINT);
    name2Selector[PARA_CONFIG_UNREGISTER_METHOD_ADDR_STR] =
        getFuncSelector(PARA_CONFIG_UNREGISTER_METHOD_ADDR_STR);
}

std::string ParallelConfigPrecompiled::toString()
{
    return "ParallelConfig";
}

PrecompiledExecResult::Ptr ParallelConfigPrecompiled::call(
    std::shared_ptr<executor::ExecutiveContext> _context, bytesConstRef _param,
    const std::string& _origin, const std::string&, u256& _remainGas)
{
    // parse function name
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);

    codec::abi::ContractABICodec abi(nullptr);
    auto callResult = m_precompiledExecResultFactory->createPrecompiledResult();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();

    if (func == name2Selector[PARA_CONFIG_REGISTER_METHOD_ADDR_STR_UINT])
    {
        registerParallelFunction(_context, data, _origin, callResult->mutableExecResult());
    }
    else if (func == name2Selector[PARA_CONFIG_UNREGISTER_METHOD_ADDR_STR])
    {
        unregisterParallelFunction(_context, data, _origin, callResult->mutableExecResult());
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ParallelConfigPrecompiled")
                               << LOG_DESC("call undefined function") << LOG_KV("func", func);
    }
    gasPricer->updateMemUsed(callResult->m_execResult.size());
    _remainGas -= gasPricer->calTotalGas();
    return callResult;
}


// TODO: use origin to check authority
TableInterface::Ptr ParallelConfigPrecompiled::openTable(
    std::shared_ptr<executor::ExecutiveContext> _context, std::string const& _contractName,
    std::string const&, bool _needCreate)
{
    std::string tableName = PARA_CONFIG_TABLE_PREFIX_SHORT + _contractName;

    auto tableFactory = _context->getTableFactory();
    if (!tableFactory)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ParallelConfigPrecompiled")
                               << LOG_DESC("TableFactoryPrecompiled has not been initialized");
        return nullptr;
    }
    auto table = tableFactory->openTable(tableName);

    if (!table && _needCreate)
    {  //__dat_transfer__ is not exist, then create it first.
        auto ret = tableFactory->createTable(tableName, PARA_KEY_NAME, PARA_VALUE_NAMES);
        if(ret)
        {
            PRECOMPILED_LOG(DEBUG)
                << LOG_BADGE("ParallelConfigPrecompiled") << LOG_DESC("open table")
                << LOG_DESC(" create parallel config table. ") << LOG_KV("tableName", tableName);
            table = tableFactory->openTable(tableName);
        }
        else
        {
            PRECOMPILED_LOG(ERROR)
                << LOG_BADGE("ParallelConfigPrecompiled")
                << LOG_DESC("create parallel config table error") << LOG_KV("tableName", tableName);
            return nullptr;
        }
    }
    return table;
}

void ParallelConfigPrecompiled::registerParallelFunction(
    std::shared_ptr<executor::ExecutiveContext> _context, bytesConstRef _data,
    std::string const& _origin, bytes& _out)
{
    // registerParallelFunctionInternal(string contractAddress, string functionName, uint256 criticalSize)

    std::string contractName;
    std::string functionName;
    u256 criticalSize;

    codec::abi::ContractABICodec abi(nullptr);
    abi.abiOut(_data, contractName, functionName, criticalSize);
    uint32_t selector = getFuncSelector(functionName);

    auto table = openTable(_context, contractName, _origin);
    if (table)
    {
        Entry::Ptr entry = table->newEntry();
        entry->setField(PARA_FUNC_NAME, functionName);
        entry->setField(PARA_CRITICAL_SIZE, boost::lexical_cast<std::string>(criticalSize));

        table->setRow(std::to_string(selector), entry);
        auto commitResult = _context->getTableFactory()->commit();
        if (!commitResult.second || commitResult.second->errorCode() == protocol::CommonError::SUCCESS)
        {
            PRECOMPILED_LOG(DEBUG)
                << LOG_BADGE("ParallelConfigPrecompiled")
                << LOG_DESC("registerParallelFunction success")
                << LOG_KV(PARA_SELECTOR, std::to_string(selector))
                << LOG_KV(PARA_FUNC_NAME, functionName) << LOG_KV(PARA_CRITICAL_SIZE, criticalSize);
            _out = abi.abiIn("", u256(0));
        }
        else
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("ParallelConfigPrecompiled")
                                   << LOG_DESC("registerParallelFunction failed, commit error")
                                   << LOG_KV("errorCode", commitResult.second->errorCode())
                                   << LOG_KV("errorMsg", commitResult.second->errorMessage());
        }

        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ParallelConfigPrecompiled") << LOG_DESC("registerParallelFunction success")
                               << LOG_KV(PARA_SELECTOR, std::to_string(selector))
                               << LOG_KV(PARA_FUNC_NAME, functionName)
                               << LOG_KV(PARA_CRITICAL_SIZE, criticalSize);
    }
}

void ParallelConfigPrecompiled::unregisterParallelFunction(
    std::shared_ptr<executor::ExecutiveContext> _context, bytesConstRef _data,
    std::string const&, bytes& _out)
{
    // unregisterParallelFunctionInternal(address,string)
    // unregisterParallelFunctionInternal(address contractAddress, string functionName)
    std::string contractAddress;
    std::string functionName;

    codec::abi::ContractABICodec abi(nullptr);
    abi.abiOut(_data, contractAddress, functionName);
    uint32_t selector = getFuncSelector(functionName);

    auto table = _context->getTableFactory()->openTable(contractAddress);
    if (table)
    {
        table->remove(std::to_string(selector));
        auto commitResult = _context->getTableFactory()->commit();
        if(!commitResult.second || commitResult.second->errorCode()==protocol::CommonError::SUCCESS)
        {
            _out = abi.abiIn("", u256(0));
            PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ParallelConfigPrecompiled")
                                   << LOG_DESC("unregisterParallelFunction success")
                                   << LOG_KV(PARA_SELECTOR, std::to_string(selector));
        }
        else
        {
            PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ParallelConfigPrecompiled")
                                   << LOG_DESC("unregisterParallelFunction commit failed")
                                   << LOG_KV("errorCode", commitResult.second->errorCode())
                                   << LOG_KV(PARA_SELECTOR, std::to_string(selector));
            // TODO: use unify code
            _out = abi.abiIn("", u256(-1));
        }
    }

}

// TODO: use origin to check authority
ParallelConfig::Ptr ParallelConfigPrecompiled::getParallelConfig(
    std::shared_ptr<executor::ExecutiveContext> _context,
    std::string const& _contractAddress, uint32_t _selector, std::string const&)
{
    auto table = _context->getTableFactory()->openTable(_contractAddress);
    if (!table)
    {
        return nullptr;
    }
    auto entry = table->getRow(std::to_string(_selector));
    if (!entry)
    {
        return nullptr;
    }
    else
    {
        std::string functionName = entry->getField(PARA_FUNC_NAME);
        u256 criticalSize;
        criticalSize = boost::lexical_cast<u256>(entry->getField(PARA_CRITICAL_SIZE));
        return std::make_shared<ParallelConfig>(ParallelConfig{functionName, criticalSize});
    }
}
