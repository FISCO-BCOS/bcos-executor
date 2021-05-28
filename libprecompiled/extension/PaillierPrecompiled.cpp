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
 * @file PaillierPrecompiled.cpp
 * @author: kyonRay
 * @date 2021-05-30
 */

#include "PaillierPrecompiled.h"
#include "../PrecompiledResult.h"
#include "../Utilities.h"
#include <bcos-framework/libcodec/abi/ContractABICodec.h>
//#include <paillier/callpaillier.h>

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::precompiled;

const char* const PAILLIER_METHOD_SET_STR = "paillierAdd(string,string)";

PaillierPrecompiled::PaillierPrecompiled()
//  : m_callPaillier(std::make_shared<CallPaillier>())
{
    name2Selector[PAILLIER_METHOD_SET_STR] = getFuncSelector(PAILLIER_METHOD_SET_STR);
}

PrecompiledExecResult::Ptr PaillierPrecompiled::call(std::shared_ptr<executor::ExecutiveContext>,
    bytesConstRef _param, const std::string&, const std::string&, u256& _remainGas)
{
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("PaillierPrecompiled") << LOG_DESC("call")
                           << LOG_KV("param", toHexString(_param));

    // parse function name
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);

    codec::abi::ContractABICodec abi(nullptr);
    auto callResult = std::make_shared<PrecompiledExecResult>();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();
    gasPricer->setMemUsed(_param.size());
    if (func == name2Selector[PAILLIER_METHOD_SET_STR])
    {
        // paillierAdd(string,string)
        std::string cipher1, cipher2;
        abi.abiOut(data, cipher1, cipher2);
        std::string result;

        // TODO: it depends on bcos-crypto
        //        try
        //        {
        //            result = m_callPaillier->paillierAdd(cipher1, cipher2);
        //            callResult->gasPricer()->appendOperation(InterfaceOpcode::PaillierAdd);
        //        }
        //        catch (CallException& e)
        //        {
        //            PRECOMPILED_LOG(ERROR)
        //                    << LOG_BADGE("PaillierPrecompiled") << LOG_DESC(std::string(e.what()))
        //                    << LOG_KV("cipher1", cipher1) << LOG_KV("cipher2", cipher2);
        //            getErrorCodeOut(callResult->mutableExecResult(), CODE_INVALID_CIPHERS);
        //            return callResult;
        //        }
        callResult->setExecResult(abi.abiIn("", result));
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("PaillierPrecompiled")
                               << LOG_DESC("call undefined function") << LOG_KV("func", func);
        getErrorCodeOut(callResult->mutableExecResult(), CODE_UNKNOW_FUNCTION_CALL);
    }
    gasPricer->updateMemUsed(callResult->m_execResult.size());
    _remainGas -= gasPricer->calTotalGas();
    return callResult;
}
