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
 * @file PermissionPrecompiled.cpp
 * @author: kyonRay
 * @date 2021-09-03
 */

#include "PermissionPrecompiled.h"
#include "../PrecompiledResult.h"
#include "../Utilities.h"

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::precompiled;

#if 0
login(string nonce, string[] params) => <s256 code, string message, string path>
logout(string path, string[] params) => <s256 code, string message>
create(string userPath, string origin, string to, bytes params) => <s256 code, string message>
call(string userPath, string origin, string to,  bytes params) => <s256 code, string message>
sendTransaction(string userPath, string origin, string to, bytes params) => <s256 code, string message>
#endif

PermissionPrecompiled::PermissionPrecompiled(crypto::Hash::Ptr _hashImpl) : PermissionPrecompiledInterface(_hashImpl)
{
    name2Selector[PER_METHOD_LOGIN] = getFuncSelector(PER_METHOD_LOGIN, _hashImpl);
    name2Selector[PER_METHOD_LOGOUT] = getFuncSelector(PER_METHOD_LOGOUT, _hashImpl);
    name2Selector[PER_METHOD_CREATE] = getFuncSelector(PER_METHOD_CREATE, _hashImpl);
    name2Selector[PER_METHOD_CALL] = getFuncSelector(PER_METHOD_CALL, _hashImpl);
    name2Selector[PER_METHOD_SEND] = getFuncSelector(PER_METHOD_SEND, _hashImpl);
}

std::string PermissionPrecompiled::toString()
{
    return "Permission";
}

std::shared_ptr<PrecompiledExecResult> PermissionPrecompiled::call(
    std::shared_ptr<executor::BlockContext> _context, bytesConstRef _param, const std::string&,
    const std::string&)
{
    // parse function name
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("PermissionPrecompiled") << LOG_DESC("call")
                           << LOG_KV("func", func);
    auto callResult = std::make_shared<PrecompiledExecResult>();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();
    gasPricer->setMemUsed(_param.size());
    if (func == name2Selector[PER_METHOD_LOGIN])
    {
        // login(string nonce, string[] params) => <s256 code, string message, string path>
        std::string nonce;
        std::vector<std::string> params;
        auto codec =
            std::make_shared<PrecompiledCodec>(_context->hashHandler(), _context->isWasm());
        codec->decode(data, nonce, params);

        auto ret = login(nonce, params);

        callResult->setExecResult(codec->encode(ret->code, ret->message, ret->path));
    }
    else if (func == name2Selector[PER_METHOD_LOGOUT])
    {
        // logout(string path, string[] params) => <s256 code, string message>
        std::string path;
        std::vector<std::string> params;
        auto codec =
            std::make_shared<PrecompiledCodec>(_context->hashHandler(), _context->isWasm());
        codec->decode(data, path, params);

        auto ret = logout(path, params);

        callResult->setExecResult(codec->encode(ret->code, ret->message));
    }
    else if (func == name2Selector[PER_METHOD_CREATE])
    {
        // create(string userPath, string origin, string to, bytes params) => <s256 code, string message>
        std::string userPath, origin, to;
        bytes params;
        auto codec =
            std::make_shared<PrecompiledCodec>(_context->hashHandler(), _context->isWasm());
        codec->decode(data, userPath, origin, to, params);

        auto ret = create(userPath, origin, to, ref(params));

        callResult->setExecResult(codec->encode(ret->code, ret->message));
    }
    else if (func == name2Selector[PER_METHOD_CALL])
    {
        // call(string userPath, string origin, string to, bytes params) => <s256 code, string message>
        std::string userPath, origin, to;
        bytes params;
        auto codec =
            std::make_shared<PrecompiledCodec>(_context->hashHandler(), _context->isWasm());
        codec->decode(data, userPath, origin, to, params);

        auto ret = call(userPath, origin, to, ref(params));

        callResult->setExecResult(codec->encode(ret->code, ret->message));
    }
    else if (func == name2Selector[PER_METHOD_SEND])
    {
        // sendTransaction(string userPath, string origin, string to, bytes params) => <s256 code, string message>
        std::string userPath, origin, to;
        bytes params;
        auto codec =
            std::make_shared<PrecompiledCodec>(_context->hashHandler(), _context->isWasm());
        codec->decode(data, userPath, origin, to, params);

        auto ret = sendTransaction(userPath, origin, to, ref(params));

        callResult->setExecResult(codec->encode(ret->code, ret->message));
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("PermissionPrecompiled") << LOG_DESC("call undefined function")
                               << LOG_KV("func", func);
    }
    gasPricer->updateMemUsed(callResult->m_execResult.size());
    callResult->setGas(gasPricer->calTotalGas());
    return callResult;
}
PermissionRet::Ptr PermissionPrecompiled::login(
    const std::string&, const std::vector<std::string>&)
{
    /// do something...
    return std::make_shared<PermissionRet>(s256((int)CODE_SUCCESS), "success", "/usr/test");
}
PermissionRet::Ptr PermissionPrecompiled::logout(
    const std::string&, const std::vector<std::string>&)
{
    /// do something...
    return std::make_shared<PermissionRet>(s256((int)CODE_SUCCESS), "success");
}
PermissionRet::Ptr PermissionPrecompiled::create(
    const std::string&, const std::string&, const std::string&, bytesConstRef)
{
    /// do something...
    return std::make_shared<PermissionRet>(s256((int)CODE_SUCCESS), "success");
}
PermissionRet::Ptr PermissionPrecompiled::call(
    const std::string&, const std::string&, const std::string&, bytesConstRef)
{
    /// do something...
    return std::make_shared<PermissionRet>(s256((int)CODE_SUCCESS), "success");
}
PermissionRet::Ptr PermissionPrecompiled::sendTransaction(
    const std::string&, const std::string&, const std::string&, bytesConstRef)
{
    /// do something...
    return std::make_shared<PermissionRet>(s256((int)CODE_SUCCESS), "success");
}
