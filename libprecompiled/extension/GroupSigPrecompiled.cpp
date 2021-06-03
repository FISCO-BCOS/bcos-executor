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
 * @file GroupSigPrecompiled.cpp
 * @author: kyonRay
 * @date 2021-05-30
 */

#include "GroupSigPrecompiled.h"
#include "../PrecompiledResult.h"
#include "../Utilities.h"
//#include <group_sig/algorithm/GroupSig.h>

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::precompiled;

const char* const GroupSig_METHOD_SET_STR = "groupSigVerify(string,string,string,string)";

GroupSigPrecompiled::GroupSigPrecompiled()
{
    name2Selector[GroupSig_METHOD_SET_STR] = getFuncSelector(GroupSig_METHOD_SET_STR);
}

PrecompiledExecResult::Ptr GroupSigPrecompiled::call(std::shared_ptr<executor::ExecutiveContext> _context,
    bytesConstRef _param, const std::string&, const std::string&, u256& _remainGas)
{
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("GroupSigPrecompiled") << LOG_DESC("call")
                           << LOG_KV("param", toHexString(_param));

    // parse function name
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);

    m_codec = std::make_shared<PrecompiledCodec>(_context->hashHandler(), _context->isWasm());
    auto callResult = std::make_shared<PrecompiledExecResult>();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();
    gasPricer->setMemUsed(_param.size());

    if (func == name2Selector[GroupSig_METHOD_SET_STR])
    {
        // groupSigVerify(string)
        std::string signature, message, gpkInfo, paramInfo;
        m_codec->decode(data, signature, message, gpkInfo, paramInfo);
        bool result = false;

        // TODO: it depends on bcos-crypto
//        try
//        {
//            result = GroupSigApi::group_verify(signature, message, gpkInfo, paramInfo);
//            callResult->gasPricer()->appendOperation(InterfaceOpcode::GroupSigVerify);
//        }
//        catch (std::string& errorMsg)
//        {
//            PRECOMPILED_LOG(ERROR) << LOG_BADGE("GroupSigPrecompiled") << LOG_DESC(errorMsg)
//                                   << LOG_KV("signature", signature) << LOG_KV("message", message)
//                                   << LOG_KV("gpkInfo", gpkInfo) << LOG_KV("paramInfo", paramInfo);
//            getErrorCodeOut(callResult->mutableExecResult(), VERIFY_GROUP_SIG_FAILED);
//            return callResult;
//        }
        callResult->setExecResult(m_codec->encode(result));
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("GroupSigPrecompiled")
                               << LOG_DESC("call undefined function") << LOG_KV("func", func);
        getErrorCodeOut(callResult->mutableExecResult(), CODE_UNKNOW_FUNCTION_CALL, m_codec);
    }
    gasPricer->updateMemUsed(callResult->m_execResult.size());
    _remainGas -= gasPricer->calTotalGas();
    return callResult;
}
