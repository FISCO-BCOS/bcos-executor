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
 * @file EntriesPrecompiled.cpp
 * @author: kyonRay
 * @date 2021-05-31
 */

#include "Common.h"
#include "Utilities.h"
#include "EntriesPrecompiled.h"
#include "EntryPrecompiled.h"
#include <bcos-framework/libcodec/abi/ContractABICodec.h>

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::precompiled;
using namespace bcos::storage;

const char* const ENTRIES_GET_INT = "get(int256)";
const char* const ENTRIES_SIZE = "size()";

EntriesPrecompiled::EntriesPrecompiled()
{
    name2Selector[ENTRIES_GET_INT] = getFuncSelector(ENTRIES_GET_INT);
    name2Selector[ENTRIES_SIZE] = getFuncSelector(ENTRIES_SIZE);
}
std::string EntriesPrecompiled::toString()
{
    return "Entries";
}
PrecompiledExecResult::Ptr EntriesPrecompiled::call(
    std::shared_ptr<executor::ExecutiveContext> _context, bytesConstRef _param,
    const std::string& _origin, const std::string& _sender, u256& _remainGas)
{
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);

    codec::abi::ContractABICodec abi(nullptr);

    auto callResult = m_precompiledExecResultFactory->createPrecompiledResult();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();
    gasPricer->setMemUsed(_param.size());

    if (func == name2Selector[ENTRIES_GET_INT])
    {  // get(int256)
        u256 num;
        abi.abiOut(data, num);

        Entry::Ptr entry = getEntriesPtr()->at(num.convert_to<size_t>());
        EntryPrecompiled::Ptr entryPrecompiled = std::make_shared<EntryPrecompiled>();
        entryPrecompiled->setEntry(entry);
        Address address = _context->registerPrecompiled(entryPrecompiled);
        callResult->setExecResult(abi.abiIn("", address));
    }
    else if (func == name2Selector[ENTRIES_SIZE])
    {  // size()
        u256 c = getEntriesConstPtr()->size();
        callResult->setExecResult(abi.abiIn("", c));
    }
    else
    {
        STORAGE_LOG(ERROR) << LOG_BADGE("EntriesPrecompiled")
                           << LOG_DESC("call undefined function!");
    }
    gasPricer->updateMemUsed(callResult->m_execResult.size());
    _remainGas -= gasPricer->calTotalGas();
    return callResult;
}