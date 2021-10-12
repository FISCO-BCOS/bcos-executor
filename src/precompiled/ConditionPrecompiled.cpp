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
 * @file ConditionPrecompiled.cpp
 * @author: kyonRay
 * @date 2021-05-31
 */

#include "ConditionPrecompiled.h"
#include "PrecompiledResult.h"
#include "Utilities.h"

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::precompiled;
using namespace bcos::protocol;

const char* const CONDITION_METHOD_EQ_STR_INT = "EQ(string,int256)";
const char* const CONDITION_METHOD_EQ_STR_STR = "EQ(string,string)";
const char* const CONDITION_METHOD_EQ_STR_ADDR = "EQ(string,address)";
const char* const CONDITION_METHOD_GE_STR_INT = "GE(string,int256)";
const char* const CONDITION_METHOD_GT_STR_INT = "GT(string,int256)";
const char* const CONDITION_METHOD_LE_STR_INT = "LE(string,int256)";
const char* const CONDITION_METHOD_LT_STR_INT = "LT(string,int256)";
const char* const CONDITION_METHOD_NE_STR_INT = "NE(string,int256)";
const char* const CONDITION_METHOD_NE_STR_STR = "NE(string,string)";
const char* const CONDITION_METHOD_LIMIT_INT = "limit(int256)";
const char* const CONDITION_METHOD_LIMIT_2INT = "limit(int256,int256)";

ConditionPrecompiled::ConditionPrecompiled(crypto::Hash::Ptr _hashImpl) : Precompiled(_hashImpl)
{
    name2Selector[CONDITION_METHOD_EQ_STR_INT] =
        getFuncSelector(CONDITION_METHOD_EQ_STR_INT, _hashImpl);
    name2Selector[CONDITION_METHOD_EQ_STR_STR] =
        getFuncSelector(CONDITION_METHOD_EQ_STR_STR, _hashImpl);
    name2Selector[CONDITION_METHOD_EQ_STR_ADDR] =
        getFuncSelector(CONDITION_METHOD_EQ_STR_ADDR, _hashImpl);
    name2Selector[CONDITION_METHOD_GE_STR_INT] =
        getFuncSelector(CONDITION_METHOD_GE_STR_INT, _hashImpl);
    name2Selector[CONDITION_METHOD_GT_STR_INT] =
        getFuncSelector(CONDITION_METHOD_GT_STR_INT, _hashImpl);
    name2Selector[CONDITION_METHOD_LE_STR_INT] =
        getFuncSelector(CONDITION_METHOD_LE_STR_INT, _hashImpl);
    name2Selector[CONDITION_METHOD_LT_STR_INT] =
        getFuncSelector(CONDITION_METHOD_LT_STR_INT, _hashImpl);
    name2Selector[CONDITION_METHOD_NE_STR_INT] =
        getFuncSelector(CONDITION_METHOD_NE_STR_INT, _hashImpl);
    name2Selector[CONDITION_METHOD_NE_STR_STR] =
        getFuncSelector(CONDITION_METHOD_NE_STR_STR, _hashImpl);
    name2Selector[CONDITION_METHOD_LIMIT_INT] =
        getFuncSelector(CONDITION_METHOD_LIMIT_INT, _hashImpl);
    name2Selector[CONDITION_METHOD_LIMIT_2INT] =
        getFuncSelector(CONDITION_METHOD_LIMIT_2INT, _hashImpl);
}

std::string ConditionPrecompiled::toString()
{
    return "Condition";
}

std::shared_ptr<PrecompiledExecResult> ConditionPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _param,
    const std::string&, const std::string&)
{
    // parse function name
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);

    STORAGE_LOG(DEBUG) << "func:" << std::hex << func;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    auto callResult = std::make_shared<PrecompiledExecResult>();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();
    gasPricer->setMemUsed(_param.size());
    assert(m_condition);
    if (func == name2Selector[CONDITION_METHOD_EQ_STR_INT])
    {
        // EQ(string,int256)
        std::string str;
        s256 num;
        codec->decode(data, str, num);

        m_condition->EQ(str, boost::lexical_cast<std::string>(num));
        gasPricer->appendOperation(InterfaceOpcode::EQ);
    }
    else if (func == name2Selector[CONDITION_METHOD_EQ_STR_STR])
    {
        // EQ(string,string)
        std::string str;
        std::string value;
        codec->decode(data, str, value);

        m_condition->EQ(str, value);
        gasPricer->appendOperation(InterfaceOpcode::EQ);
    }
    else if (func == name2Selector[CONDITION_METHOD_EQ_STR_ADDR])
    {
        // EQ(string,address)
        std::string str;
        Address value;
        codec->decode(data, str, value);
        m_condition->EQ(str, value.hex());
        gasPricer->appendOperation(InterfaceOpcode::EQ);
    }
    else if (func == name2Selector[CONDITION_METHOD_GE_STR_INT])
    {
        // GE(string,int256)
        std::string str;
        s256 value;
        codec->decode(data, str, value);

        m_condition->GE(str, boost::lexical_cast<std::string>(value));
        gasPricer->appendOperation(InterfaceOpcode::GE);
    }
    else if (func == name2Selector[CONDITION_METHOD_GT_STR_INT])
    {
        // GT(string,int256)
        std::string str;
        s256 value;
        codec->decode(data, str, value);

        m_condition->GT(str, boost::lexical_cast<std::string>(value));
        gasPricer->appendOperation(InterfaceOpcode::GT);
    }
    else if (func == name2Selector[CONDITION_METHOD_LE_STR_INT])
    {
        // LE(string,int256)
        std::string str;
        s256 value;
        codec->decode(data, str, value);

        m_condition->LE(str, boost::lexical_cast<std::string>(value));
        gasPricer->appendOperation(InterfaceOpcode::LE);
    }
    else if (func == name2Selector[CONDITION_METHOD_LT_STR_INT])
    {
        // LT(string,int256)
        std::string str;
        s256 value;
        codec->decode(data, str, value);

        m_condition->LT(str, boost::lexical_cast<std::string>(value));
        gasPricer->appendOperation(InterfaceOpcode::LT);
    }
    else if (func == name2Selector[CONDITION_METHOD_NE_STR_INT])
    {
        // NE(string,int256)
        std::string str;
        s256 num;
        codec->decode(data, str, num);

        m_condition->NE(str, boost::lexical_cast<std::string>(num));
        gasPricer->appendOperation(InterfaceOpcode::NE);
    }
    else if (func == name2Selector[CONDITION_METHOD_NE_STR_STR])
    {
        // NE(string,string)
        std::string str;
        std::string value;
        codec->decode(data, str, value);

        m_condition->NE(str, value);
        gasPricer->appendOperation(InterfaceOpcode::NE);
    }
    else if (func == name2Selector[CONDITION_METHOD_LIMIT_INT])
    {
        // limit(int256)
        s256 num;
        codec->decode(data, num);
        num = (num < 0) ? 0 : num;

        m_condition->limit(size_t(num));
        gasPricer->appendOperation(InterfaceOpcode::Limit);
    }
    else if (func == name2Selector[CONDITION_METHOD_LIMIT_2INT])
    {
        // limit(int256,int256)
        s256 start;
        s256 end;
        codec->decode(data, start, end);
        start = (start < 0) ? 0 : start;
        end = (end < start) ? start : end;

        m_condition->limit(size_t(start), size_t(end));
        gasPricer->appendOperation(InterfaceOpcode::Limit);
    }
    else
    {
        STORAGE_LOG(ERROR) << LOG_BADGE("ConditionPrecompiled")
                           << LOG_DESC("call undefined function!");
    }
    gasPricer->updateMemUsed(callResult->m_execResult.size());
    callResult->setGas(gasPricer->calTotalGas());
    return callResult;
}
