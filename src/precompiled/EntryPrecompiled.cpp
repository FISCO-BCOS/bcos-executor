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
 * @file EntryPrecompiled.cpp
 * @author: kyonRay
 * @date 2021-05-26
 */

#include "EntryPrecompiled.h"
#include "PrecompiledResult.h"
#include "Utilities.h"
#include <bcos-framework/interfaces/storage/Table.h>
#include <bcos-framework/libutilities/Common.h>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::storage;

const char* const ENTRY_GET_INT = "getInt(string)";
const char* const ENTRY_GET_UINT = "getUInt(string)";
const char* const ENTRY_SET_STR_INT = "set(string,int256)";
const char* const ENTRY_SET_STR_UINT = "set(string,uint256)";
const char* const ENTRY_SET_STR_ADDR = "set(string,address)";
const char* const ENTRY_SET_STR_STR = "set(string,string)";
const char* const ENTRY_GETA_STR = "getAddress(string)";
const char* const ENTRY_GETB_STR = "getBytes64(string)";
const char* const ENTRY_GETB_STR32 = "getBytes32(string)";
const char* const ENTRY_GET_STR = "getString(string)";

EntryPrecompiled::EntryPrecompiled(crypto::Hash::Ptr _hashImpl) : Precompiled(_hashImpl)
{
    name2Selector[ENTRY_GET_INT] = getFuncSelector(ENTRY_GET_INT, _hashImpl);
    name2Selector[ENTRY_GET_UINT] = getFuncSelector(ENTRY_GET_UINT, _hashImpl);
    name2Selector[ENTRY_SET_STR_INT] = getFuncSelector(ENTRY_SET_STR_INT, _hashImpl);
    name2Selector[ENTRY_SET_STR_UINT] = getFuncSelector(ENTRY_SET_STR_UINT, _hashImpl);
    name2Selector[ENTRY_SET_STR_STR] = getFuncSelector(ENTRY_SET_STR_STR, _hashImpl);
    name2Selector[ENTRY_SET_STR_ADDR] = getFuncSelector(ENTRY_SET_STR_ADDR, _hashImpl);
    name2Selector[ENTRY_GETA_STR] = getFuncSelector(ENTRY_GETA_STR, _hashImpl);
    name2Selector[ENTRY_GETB_STR] = getFuncSelector(ENTRY_GETB_STR, _hashImpl);
    name2Selector[ENTRY_GETB_STR32] = getFuncSelector(ENTRY_GETB_STR32, _hashImpl);
    name2Selector[ENTRY_GET_STR] = getFuncSelector(ENTRY_GET_STR, _hashImpl);
}

std::string EntryPrecompiled::toString()
{
    return "Entry";
}

PrecompiledExecResult::Ptr EntryPrecompiled::call(std::shared_ptr<executor::BlockContext> _context,
    bytesConstRef _param, const std::string&, const std::string&, int64_t _remainGas)
{
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);
    auto codec = std::make_shared<PrecompiledCodec>(_context->hashHandler(), _context->isWasm());
    auto callResult = std::make_shared<PrecompiledExecResult>();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();
    gasPricer->setMemUsed(_param.size());

    if (func == name2Selector[ENTRY_GET_INT])
    {
        // getInt(string)
        std::string str;
        codec->decode(data, str);
        s256 num = boost::lexical_cast<s256>(m_entry->getField(str));
        gasPricer->appendOperation(InterfaceOpcode::GetInt);
        callResult->setExecResult(codec->encode(num));
    }
    else if (func == name2Selector[ENTRY_GET_UINT])
    {
        // getUInt(string)
        std::string str;
        codec->decode(data, str);
        u256 num = boost::lexical_cast<u256>(m_entry->getField(str));
        gasPricer->appendOperation(InterfaceOpcode::GetInt);
        callResult->setExecResult(codec->encode(num));
    }
    else if (func == name2Selector[ENTRY_SET_STR_INT])
    {
        // set(string,int256)
        std::string key;
        s256 num;
        codec->decode(data, key, num);
        auto value = boost::lexical_cast<std::string>(num);
        m_entry->setField(key, value);
        gasPricer->appendOperation(InterfaceOpcode::Set);
    }
    else if (func == name2Selector[ENTRY_SET_STR_UINT])
    {
        // set(string,uint256)
        std::string key;
        u256 num;
        codec->decode(data, key, num);
        auto value = boost::lexical_cast<std::string>(num);
        m_entry->setField(key, value);
        gasPricer->appendOperation(InterfaceOpcode::Set);
    }
    else if (func == name2Selector[ENTRY_SET_STR_STR])
    {
        // set(string,string)
        std::string str;
        std::string value;
        codec->decode(data, str, value);

        m_entry->setField(str, value);
        gasPricer->appendOperation(InterfaceOpcode::Set);
    }
    else if (func == name2Selector[ENTRY_SET_STR_ADDR])
    {
        // set(string,address)
        std::string str;
        Address value;
        codec->decode(data, str, value);

        m_entry->setField(str, value.hex());
        gasPricer->appendOperation(InterfaceOpcode::Set);
    }
    else if (func == name2Selector[ENTRY_GETA_STR])
    {
        // getAddress(string)
        std::string str;
        codec->decode(data, str);

        auto value = m_entry->getField(str);
        if (_context->isWasm())
        {
            callResult->setExecResult(codec->encode(std::string(value)));
        }
        else
        {
            auto ret = Address(std::string(value));
            callResult->setExecResult(codec->encode(ret));
        }
        gasPricer->appendOperation(InterfaceOpcode::GetAddr);
    }
    else if (func == name2Selector[ENTRY_GETB_STR])
    {
        // getBytes64(string)
        std::string str;
        codec->decode(data, str);

        auto value = m_entry->getField(str);

        string32 ret0;
        string32 ret1;

        for (unsigned i = 0; i < 32; ++i)
            ret0[i] = (i < value.size() ? value[i] : 0);

        for (unsigned i = 32; i < 64; ++i)
            ret1[i - 32] = (i < value.size() ? value[i] : 0);
        callResult->setExecResult(codec->encode(ret0, ret1));
        gasPricer->appendOperation(InterfaceOpcode::GetByte64);
    }
    else if (func == name2Selector[ENTRY_GETB_STR32])
    {
        // getBytes32(string)
        std::string str;
        codec->decode(data, str);

        auto value = m_entry->getField(str);
        bcos::string32 s32 = bcos::codec::toString32(std::string(value));
        callResult->setExecResult(codec->encode(s32));
        gasPricer->appendOperation(InterfaceOpcode::GetByte32);
    }
    else if (func == name2Selector[ENTRY_GET_STR])
    {
        // getString(string)
        std::string str;
        codec->decode(data, str);

        auto value = m_entry->getField(str);
        callResult->setExecResult(codec->encode(std::string(value)));
        gasPricer->appendOperation(InterfaceOpcode::GetString);
    }
    else
    {
        STORAGE_LOG(ERROR) << LOG_BADGE("EntryPrecompiled") << LOG_DESC("call undefined function!");
    }
    gasPricer->updateMemUsed(callResult->m_execResult.size());
    _remainGas -= gasPricer->calTotalGas();
    return callResult;
}
