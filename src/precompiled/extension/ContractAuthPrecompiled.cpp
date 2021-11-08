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
 * @file ContractAuthPrecompiled.cpp
 * @author: kyonRay
 * @date 2021-10-09
 */

#include "ContractAuthPrecompiled.h"

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;

const char* const AUTH_METHOD_GET_ADMIN = "getAdmin(string)";
const char* const AUTH_METHOD_SET_ADMIN = "resetAdmin(string,address)";
const char* const AUTH_METHOD_SET_AUTH_TYPE = "setMethodAuthType(string,bytes4,uint8)";
const char* const AUTH_METHOD_OPEN_AUTH = "openMethodAuth(string,bytes4,address)";
const char* const AUTH_METHOD_CLOSE_AUTH = "closeMethodAuth(string,bytes4,address)";
const char* const AUTH_METHOD_CHECK_AUTH = "checkMethodAuth(string,bytes4,address)";

ContractAuthPrecompiled::ContractAuthPrecompiled(crypto::Hash::Ptr _hashImpl)
  : Precompiled(_hashImpl)
{
    name2Selector[AUTH_METHOD_GET_ADMIN] = getFuncSelector(AUTH_METHOD_GET_ADMIN, _hashImpl);
    name2Selector[AUTH_METHOD_SET_ADMIN] = getFuncSelector(AUTH_METHOD_SET_ADMIN, _hashImpl);
    name2Selector[AUTH_METHOD_SET_AUTH_TYPE] =
        getFuncSelector(AUTH_METHOD_SET_AUTH_TYPE, _hashImpl);
    name2Selector[AUTH_METHOD_OPEN_AUTH] = getFuncSelector(AUTH_METHOD_OPEN_AUTH, _hashImpl);
    name2Selector[AUTH_METHOD_CLOSE_AUTH] = getFuncSelector(AUTH_METHOD_CLOSE_AUTH, _hashImpl);
    name2Selector[AUTH_METHOD_CHECK_AUTH] = getFuncSelector(AUTH_METHOD_CHECK_AUTH, _hashImpl);
}

std::shared_ptr<PrecompiledExecResult> ContractAuthPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _param,
    const std::string&, const std::string& _sender)
{
    // parse function name
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("ContractAuthPrecompiled") << LOG_DESC("call")
                           << LOG_KV("func", func);
    auto callResult = std::make_shared<PrecompiledExecResult>();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();

    gasPricer->setMemUsed(_param.size());

    if (func == name2Selector[AUTH_METHOD_GET_ADMIN])
    {
        getAdmin(_executive, data, callResult, gasPricer);
    }
    else if (func == name2Selector[AUTH_METHOD_SET_ADMIN])
    {
        resetAdmin(_executive, data, callResult, _sender, gasPricer);
    }
    else if (func == name2Selector[AUTH_METHOD_SET_AUTH_TYPE])
    {
        setMethodAuthType(_executive, data, callResult, _sender, gasPricer);
    }
    else if (func == name2Selector[AUTH_METHOD_OPEN_AUTH])
    {
        openMethodAuth(_executive, data, callResult, _sender, gasPricer);
    }
    else if (func == name2Selector[AUTH_METHOD_CLOSE_AUTH])
    {
        closeMethodAuth(_executive, data, callResult, _sender, gasPricer);
    }
    else if (func == name2Selector[AUTH_METHOD_CHECK_AUTH])
    {
        checkMethodAuth(_executive, data, callResult, _sender, gasPricer);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled")
                               << LOG_DESC("call undefined function") << LOG_KV("func", func);
    }
    gasPricer->updateMemUsed(callResult->m_execResult.size());
    callResult->setGas(gasPricer->calTotalGas());
    return callResult;
}

void ContractAuthPrecompiled::getAdmin(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const PrecompiledGas::Ptr& gasPricer)
{
    std::string path;
    Address admin;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, path);
    path = getAuthTableName(path);
    auto table = _executive->storage().openTable(path);
    if (!table)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled") << LOG_DESC("path not found")
                               << LOG_KV("path", path);
        callResult->setExecResult(codec->encode(std::string("")));
        return;
    }
    auto entry = table->getRow(ADMIN_FIELD);
    if (!entry)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled")
                               << LOG_DESC("entry not found") << LOG_KV("path", path);
        callResult->setExecResult(codec->encode(std::string("")));
        return;
    }
    admin = Address(std::string(entry->getField(ADMIN_FIELD)), Address::FromBinary);
    gasPricer->updateMemUsed(1);
    callResult->setExecResult(codec->encode(admin));
}

void ContractAuthPrecompiled::resetAdmin(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const std::string& _sender,
    const PrecompiledGas::Ptr& gasPricer)
{
    std::string path;
    Address admin;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, path, admin);
    // check _sender from /sys/
    if (!checkSender(_sender))
    {
        getErrorCodeOut(callResult->mutableExecResult(), CODE_NO_AUTHORIZED, codec);
        return;
    }
    path = getAuthTableName(path);
    auto table = _executive->storage().openTable(path);
    if (!table || !table->getRow(ADMIN_FIELD))
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled") << LOG_DESC("path not found")
                               << LOG_KV("path", path);
        getErrorCodeOut(callResult->mutableExecResult(), CODE_TABLE_AGENT_ROW_NOT_EXIST, codec);
        return;
    }
    auto newEntry = table->newEntry();
    newEntry.setField(SYS_VALUE, asString(admin.asBytes()));
    table->setRow(ADMIN_FIELD, std::move(newEntry));
    gasPricer->updateMemUsed(1);
    gasPricer->appendOperation(InterfaceOpcode::Set);
    getErrorCodeOut(callResult->mutableExecResult(), CODE_SUCCESS, codec);
}

void ContractAuthPrecompiled::setMethodAuthType(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const std::string& _sender,
    const PrecompiledGas::Ptr& gasPricer)
{
    std::string path;
    bytes func;
    u256 type;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, path, func, type);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractAuthPrecompiled") << LOG_DESC("setMethodAuthType")
                           << LOG_KV("path", path) << LOG_KV("func", asString(func))
                           << LOG_KV("type", type);
    // check _sender from /sys/
    if (!checkSender(_sender))
    {
        getErrorCodeOut(callResult->mutableExecResult(), CODE_NO_AUTHORIZED, codec);
        return;
    }
    path = getAuthTableName(path);
    auto table = _executive->storage().openTable(path);
    if (!table)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled") << LOG_DESC("path not found")
                               << LOG_KV("path", path);
        getErrorCodeOut(callResult->mutableExecResult(), CODE_TABLE_NOT_EXIST, codec);
        return;
    }
    auto entry = table->getRow(METHOD_AUTH_TYPE);
    if (!entry)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled")
                               << LOG_DESC("method_auth_type row not found")
                               << LOG_KV("path", path);
        getErrorCodeOut(callResult->mutableExecResult(), CODE_TABLE_AUTH_ROW_NOT_EXIST, codec);
        return;
    }
    std::string authTypeStr = std::string(entry->getField(SYS_VALUE));
    std::map<bytes, u256> methAuthTypeMap;
    if (!authTypeStr.empty())
    {
        codec::scale::decode(methAuthTypeMap, gsl::make_span(asBytes(authTypeStr)));
    }
    // covered writing
    methAuthTypeMap[func] = type;
    entry->setField(SYS_VALUE, asString(codec::scale::encode(methAuthTypeMap)));
    table->setRow(METHOD_AUTH_TYPE, std::move(entry.value()));
    gasPricer->updateMemUsed(1);
    gasPricer->appendOperation(InterfaceOpcode::Set);
    getErrorCodeOut(callResult->mutableExecResult(), CODE_SUCCESS, codec);
}

void ContractAuthPrecompiled::openMethodAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const std::string& _sender,
    const PrecompiledGas::Ptr& gasPricer)
{
    setMethodAuth(_executive, data, callResult, false, _sender, gasPricer);
}

void ContractAuthPrecompiled::closeMethodAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const std::string& _sender,
    const PrecompiledGas::Ptr& gasPricer)
{
    setMethodAuth(_executive, data, callResult, true, _sender, gasPricer);
}

void ContractAuthPrecompiled::checkMethodAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const std::string& _sender,
    const PrecompiledGas::Ptr&)
{
    std::string path;
    bytes func;
    Address account;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, path, func, account);
    bool result = false;
    path = getAuthTableName(path);
    auto table = _executive->storage().openTable(path);
    if (!table)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled") << LOG_DESC("path not found")
                               << LOG_KV("path", path);
        callResult->setExecResult(codec->encode(result));
        return;
    }
    auto getMethodType = getMethodAuthType(table, ref(func));
    if (getMethodType == (int)CODE_TABLE_AUTH_TYPE_NOT_EXIST)
    {
        callResult->setExecResult(codec->encode(true));
        return;
    }
    std::string getTypeStr;
    if (getMethodType == 1)
        getTypeStr = METHOD_AUTH_WHITE;
    else if (getMethodType == 2)
        getTypeStr = METHOD_AUTH_BLACK;
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled")
                               << LOG_DESC("error auth type") << LOG_KV("path", path)
                               << LOG_KV("type", getMethodType);
        callResult->setExecResult(codec->encode(result));
        return;
    }

    auto entry = table->getRow(getTypeStr);
    if (!entry && !entry->getField(SYS_VALUE).empty())
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled")
                               << LOG_DESC("auth row not found") << LOG_KV("path", path);
        callResult->setExecResult(codec->encode(result));
        return;
    }
    MethodAuthMap authMap;
    codec::scale::decode(authMap, gsl::make_span(asBytes(std::string(entry->getField(SYS_VALUE)))));
    if (authMap.find(func) != authMap.end())
    {
        try
        {
            auto access = authMap.at(func).at(account);
            result = (getMethodType == 1) ? access : !access;
        }
        catch (...)
        {
            // can't find account in user map, return false?
            result = false;
        }
    }
    callResult->setExecResult(codec->encode(result));
}

void ContractAuthPrecompiled::setMethodAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, bool _isClose,
    const std::string& _sender, const PrecompiledGas::Ptr& gasPricer)
{
    std::string path;
    Address account;
    bytes func;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, path, func, account);
    PRECOMPILED_LOG(INFO) << LOG_BADGE("ContractAuthPrecompiled") << LOG_DESC("setAuth")
                          << LOG_KV("path", path) << LOG_KV("func", asString(func))
                          << LOG_KV("account", account.hex());
    // check _sender from /sys/
    if (!checkSender(_sender))
    {
        getErrorCodeOut(callResult->mutableExecResult(), CODE_NO_AUTHORIZED, codec);
        return;
    }
    path = getAuthTableName(path);
    auto table = _executive->storage().openTable(path);
    if (!table)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled") << LOG_DESC("path not found")
                               << LOG_KV("path", path);
        getErrorCodeOut(callResult->mutableExecResult(), CODE_TABLE_NOT_EXIST, codec);
        return;
    }
    s256 authType = getMethodAuthType(table, ref(func));
    if (authType <= 0)
    {
        callResult->setExecResult(codec->encode(authType));
        return;
    }
    std::string getTypeStr;
    if (authType == 1)
        getTypeStr = METHOD_AUTH_WHITE;
    else if (authType == 2)
        getTypeStr = METHOD_AUTH_BLACK;
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled")
                               << LOG_DESC("error auth type") << LOG_KV("path", path)
                               << LOG_KV("type", authType);
        getErrorCodeOut(callResult->mutableExecResult(), CODE_TABLE_ERROR_AUTH_TYPE, codec);
        return;
    }
    auto entry = table->getRow(getTypeStr);
    if (entry && !entry->getField(SYS_VALUE).empty())
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled")
                               << LOG_DESC("auth row not found") << LOG_KV("path", path);
        getErrorCodeOut(callResult->mutableExecResult(), CODE_TABLE_AUTH_ROW_NOT_EXIST, codec);
        return;
    }

    MethodAuthMap authMap;
    codec::scale::decode(authMap, gsl::make_span(asBytes(std::string(entry->getField(SYS_VALUE)))));
    if (authMap.empty())
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled")
                               << LOG_DESC("auth map parse error") << LOG_KV("path", path);
        getErrorCodeOut(callResult->mutableExecResult(), CODE_TABLE_AUTH_ROW_NOT_EXIST, codec);
        return;
    }
    bool access = _isClose ? (authType == 2) : (authType == 1);

    auto userAuth = std::make_pair(account, access);
    if (authMap.find(func) != authMap.end())
    {
        authMap.at(func)[account] = access;
    }
    else
    {
        // first insert func
        authMap.insert({func, {std::move(userAuth)}});
    }
    entry->setField(SYS_VALUE, asString(codec::scale::encode(authMap)));
    table->setRow(getTypeStr, std::move(entry.value()));
    gasPricer->updateMemUsed(1);
    gasPricer->appendOperation(InterfaceOpcode::Set);
    getErrorCodeOut(callResult->mutableExecResult(), CODE_SUCCESS, codec);
}


s256 ContractAuthPrecompiled::getMethodAuthType(
    std::optional<storage::Table> _table, bytesConstRef _func)
{
    // _table can't be nullopt
    auto entry = _table->getRow(METHOD_AUTH_TYPE);
    if (!entry)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled")
                               << LOG_DESC("auth type row not found");
        return (int)CODE_TABLE_AUTH_ROW_NOT_EXIST;
    }
    std::string authTypeStr = std::string(entry->getField(SYS_VALUE));
    std::map<bytes, u256> authTypeMap;
    if (authTypeStr.empty())
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractAuthPrecompiled")
                               << LOG_DESC("should set the method access auth type firstly");
        return (int)CODE_TABLE_AUTH_TYPE_NOT_EXIST;
    }
    else
    {
        codec::scale::decode(authTypeMap, gsl::make_span(asBytes(authTypeStr)));
        s256 type = -1;
        try
        {
            type = u2s(authTypeMap.at(_func.toBytes()));
        }
        catch (...)
        {
            PRECOMPILED_LOG(ERROR)
                << LOG_BADGE("ContractAuthPrecompiled") << LOG_DESC("decode method type error");
            return (int)CODE_TABLE_AUTH_TYPE_DECODE_ERROR;
        }
        return type;
    }
}
