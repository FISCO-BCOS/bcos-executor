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
#include "../PrecompiledResult.h"
#include <json/json.h>
#include "../Utilities.h"

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;

const char* const AUTH_METHOD_GET_AGENT = "agent(string)";
const char* const AUTH_METHOD_SET_AGENT = "setAgent(string,string)";
const char* const AUTH_METHOD_SET_AUTH = "setAuth(string,bytes,string,bool)";
const char* const AUTH_METHOD_CHECK_AUTH = "checkAuth(string,bytes,string)";

ContractAuthPrecompiled::ContractAuthPrecompiled(crypto::Hash::Ptr _hashImpl)
  : Precompiled(_hashImpl)
{
    name2Selector[AUTH_METHOD_GET_AGENT] = getFuncSelector(AUTH_METHOD_GET_AGENT, _hashImpl);
    name2Selector[AUTH_METHOD_SET_AGENT] = getFuncSelector(AUTH_METHOD_SET_AGENT, _hashImpl);
    name2Selector[AUTH_METHOD_SET_AUTH] = getFuncSelector(AUTH_METHOD_SET_AUTH, _hashImpl);
    name2Selector[AUTH_METHOD_CHECK_AUTH] = getFuncSelector(AUTH_METHOD_CHECK_AUTH, _hashImpl);
}

std::shared_ptr<PrecompiledExecResult> ContractAuthPrecompiled::call(
    std::shared_ptr<executor::BlockContext> _context, bytesConstRef _param,
    const std::string& _origin, const std::string& _sender)
{
    // parse function name
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("ContractAuthPrecompiled") << LOG_DESC("call")
                           << LOG_KV("func", func);
    auto callResult = std::make_shared<PrecompiledExecResult>();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();

    gasPricer->setMemUsed(_param.size());

    if (func == name2Selector[AUTH_METHOD_GET_AGENT])
    {
        agent(_context, data, callResult, gasPricer);
    }
    else if (func == name2Selector[AUTH_METHOD_SET_AGENT])
    {
        setAgent(_context, data, callResult, _origin, _sender, gasPricer);
    }
    else if (func == name2Selector[AUTH_METHOD_SET_AUTH])
    {
        setAuth(_context, data, callResult, _origin, _sender, gasPricer);
    }
    else if (func == name2Selector[AUTH_METHOD_CHECK_AUTH])
    {
        checkAuth(_context, data, callResult, gasPricer);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled") << LOG_DESC("call undefined function")
                               << LOG_KV("func", func);
    }
    gasPricer->updateMemUsed(callResult->m_execResult.size());
    callResult->setGas(gasPricer->calTotalGas());
    return callResult;
}

void ContractAuthPrecompiled::agent(const std::shared_ptr<executor::BlockContext>& _context,
    bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
    const PrecompiledGas::Ptr& gasPricer)
{
    std::string path, agent;
    auto codec = std::make_shared<PrecompiledCodec>(_context->hashHandler(), _context->isWasm());
    codec->decode(data, path);
    auto table = _context->storage()->openTable(path);
    if (!table)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled") << LOG_DESC("path not found")
                               << LOG_KV("path", path);
        callResult->setExecResult(codec->encode(std::string("")));
        return;
    }
    auto entry = table->getRow(AGENT_FILED);
    if (!entry)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled") << LOG_DESC("entry not found")
                               << LOG_KV("path", path);
        callResult->setExecResult(codec->encode(std::string("")));
        return;
    }
    agent = entry->getField(SYS_VALUE);
    gasPricer->updateMemUsed(1);
    callResult->setExecResult(codec->encode(std::move(agent)));
}

void ContractAuthPrecompiled::setAgent(const std::shared_ptr<executor::BlockContext>& _context,
    bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
    const std::string&, const std::string& _sender, const PrecompiledGas::Ptr& gasPricer)
{
    std::string path, agent;
    auto codec = std::make_shared<PrecompiledCodec>(_context->hashHandler(), _context->isWasm());
    codec->decode(data, path, agent);
    // check _sender from /sys/
    if(!checkSender(_sender))
    {
        getErrorCodeOut(callResult->mutableExecResult(), CODE_NO_AUTHORIZED, codec);
        return;
    }
    auto table = _context->storage()->openTable(path);
    if (!table || !table->getRow(AGENT_FILED))
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled") << LOG_DESC("path not found")
                               << LOG_KV("path", path);
        getErrorCodeOut(callResult->mutableExecResult(), CODE_TABLE_AGENT_ROW_NOT_EXIST, codec);
        return;
    }
    // TODO: check agent is a valid string
    auto newEntry = table->newEntry();
    newEntry.setField(SYS_VALUE, agent);
    table->setRow(AGENT_FILED, std::move(newEntry));
    gasPricer->updateMemUsed(1);
    gasPricer->appendOperation(InterfaceOpcode::Set);
    getErrorCodeOut(callResult->mutableExecResult(), CODE_SUCCESS, codec);
}

void ContractAuthPrecompiled::setAuth(const std::shared_ptr<executor::BlockContext>& _context,
    bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
    const std::string&, const std::string& _sender, const PrecompiledGas::Ptr& gasPricer)
{
    std::string path, user;
    bytes func;
    bool access;
    auto codec = std::make_shared<PrecompiledCodec>(_context->hashHandler(), _context->isWasm());
    codec->decode(data, path, func, user, access);
    PRECOMPILED_LOG(INFO) << LOG_BADGE("ContractAuthPrecompiled") << LOG_DESC("setAuth")
                          << LOG_KV("path", path) << LOG_KV("func", asString(func))
                          << LOG_KV("user", user);
    // check _sender from /sys/
    if(!checkSender(_sender))
    {
        getErrorCodeOut(callResult->mutableExecResult(), CODE_NO_AUTHORIZED, codec);
        return;
    }
    auto table = _context->storage()->openTable(path);
    if (!table)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled") << LOG_DESC("path not found")
                               << LOG_KV("path", path);
        getErrorCodeOut(callResult->mutableExecResult(), CODE_TABLE_NOT_EXIST, codec);
        return;
    }
    auto entry = table->getRow(INTERFACE_AUTH);
    if (!entry)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled")
                               << LOG_DESC("auth row not found") << LOG_KV("path", path);
        getErrorCodeOut(callResult->mutableExecResult(), CODE_TABLE_AUTH_ROW_NOT_EXIST, codec);
        return;
    }
    std::map<bytes, std::vector<std::pair<std::string, bool>>> authMap;
    json2AuthMap(std::string(entry->getField(SYS_VALUE)), authMap);
    if (authMap.empty())
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled")
                               << LOG_DESC("auth map parse error") << LOG_KV("path", path);
        getErrorCodeOut(callResult->mutableExecResult(), CODE_TABLE_AUTH_ROW_NOT_EXIST, codec);
        return;
    }
    auto userAuth = std::make_pair(std::move(user), access);
    if (authMap.find(func) != authMap.end())
    {
        authMap.at(func).emplace_back(std::move(userAuth));
    }
    else
    {
        authMap.insert({func, {std::move(userAuth)}});
    }
    entry->setField(SYS_VALUE, authMap2Json(authMap));
    table->setRow(INTERFACE_AUTH, std::move(entry.value()));
    gasPricer->updateMemUsed(1);
    gasPricer->appendOperation(InterfaceOpcode::Set);
    getErrorCodeOut(callResult->mutableExecResult(), CODE_SUCCESS, codec);
}

void ContractAuthPrecompiled::checkAuth(const std::shared_ptr<executor::BlockContext>& _context,
    bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
    const PrecompiledGas::Ptr& gasPricer)
{
    std::string path, user;
    bytes func;
    auto codec = std::make_shared<PrecompiledCodec>(_context->hashHandler(), _context->isWasm());
    codec->decode(data, path, func, user);
    bool result = false;

    auto table = _context->storage()->openTable(path);
    if (!table)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled") << LOG_DESC("path not found")
                               << LOG_KV("path", path);
        callResult->setExecResult(codec->encode(result));
        return;
    }
    auto entry = table->getRow(INTERFACE_AUTH);
    if (!entry)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled")
                               << LOG_DESC("auth row not found") << LOG_KV("path", path);
        callResult->setExecResult(codec->encode(result));
        return;
    }
    std::map<bytes, std::vector<std::pair<std::string, bool>>> authMap;
    json2AuthMap(std::string(entry->getField(SYS_VALUE)), authMap);
    if (authMap.find(func) != authMap.end())
    {
        for (const auto &userAuth : authMap.at(func)){
            if(userAuth.first == user && userAuth.second){
                result = true;
                break;
            }
        }
    }
    callResult->setExecResult(codec->encode(result));
}

std::string ContractAuthPrecompiled::authMap2Json(
    const std::map<bytes, std::vector<std::pair<std::string, bool>>>& _authMap)
{
    Json::Value authObj;
    for (const auto& funcAuthPair : _authMap)
    {
        Json::Value userAuth(Json::arrayValue);
        for (const auto &userAuthPair : funcAuthPair.second)
        {
            Json::Value user;
            user[userAuthPair.first] = userAuthPair.second;
            userAuth.append(user);
        }
        authObj[asString(funcAuthPair.first)] = userAuth;
    }
    return authObj.asString();
}

void ContractAuthPrecompiled::json2AuthMap(
    const std::string& _json, std::map<bytes, std::vector<std::pair<std::string, bool>>>& _authMap)
{
    Json::Reader reader;
    Json::Value value;
    if(reader.parse(_json, value))
    {
        try
        {
            Json::Value::Members members = value.getMemberNames();
            for (auto& member : members)
            {
                // std::vector
                Json::Value userAuthJson = value[member];
                std::vector<std::pair<std::string, bool>> userVector;
                for (unsigned int i = 0; i < value[member].size(); i++)
                {
                    std::string userName = value[member][i].getMemberNames().at(0);
                    bool access = value[member][i][userName].asBool();
                    auto userAuth = std::make_pair(std::move(userName), access);
                    userVector.emplace_back(std::move(userAuth));
                }
                _authMap.insert(std::make_pair(asBytes(member), userVector));
            }
        }
        catch (...){
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled")
                                   << LOG_DESC("jsonParse error") << LOG_KV("json", _json);
        }
    }
}