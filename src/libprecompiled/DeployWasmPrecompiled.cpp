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
 * @file deployWasmPrecompiled.cpp
 * @author: kyonRay
 * @date 2021-06-17
 */

#include "DeployWasmPrecompiled.h"
#include "../libvm/TransactionExecutive.h"
#include "Common.h"
#include "PrecompiledResult.h"
#include "Utilities.h"
#include <bcos-framework/interfaces/protocol/Exceptions.h>

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::precompiled;
using namespace bcos::protocol;

const char* const WASM_METHOD_DEPLOY = "deployWasm(bytes,bytes,string,string)";

DeployWasmPrecompiled::DeployWasmPrecompiled(crypto::Hash::Ptr _hashImpl)
  : precompiled::Precompiled(_hashImpl)
{
    name2Selector[WASM_METHOD_DEPLOY] = getFuncSelector(WASM_METHOD_DEPLOY, _hashImpl);
}
std::string DeployWasmPrecompiled::toString()
{
    return "DeployWasm";
}
std::shared_ptr<PrecompiledExecResult> DeployWasmPrecompiled::call(
    std::shared_ptr<executor::BlockContext> _context, bytesConstRef _param,
    const std::string& _origin, const std::string& _sender, u256& _remainGas)
{
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("DeployWasmPrecompiled") << LOG_DESC("call")
                           << LOG_KV("func", func);
    m_codec = std::make_shared<PrecompiledCodec>(_context->hashHandler(), _context->isWasm());
    auto callResult = std::make_shared<PrecompiledExecResult>();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();
    gasPricer->setMemUsed(_param.size());
    if (func == name2Selector[WASM_METHOD_DEPLOY])
    {
        bytes code, param;
        std::string path, jsonABI;
        m_codec->decode(data, code, param, path, jsonABI);
        auto table = _context->getTableFactory()->openTable(path);
        if (table)
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("DeployWasmPrecompiled")
                                   << LOG_DESC("path exist, create error") << LOG_KV("path", path);
            BOOST_THROW_EXCEPTION(protocol::PrecompiledError());
        }
        else
        {
            // build dir
            std::string parentDir = path.substr(0, path.find_last_of('/'));
            std::string contractName = path.substr(path.find_last_of('/'));
            recursiveBuildDir(_context->getTableFactory(), parentDir);
            // TODO: get critical domains in jsonABI
            auto executive = std::make_shared<TransactionExecutive>(_context);
            if (!executive->executeCreate(
                    _sender, _origin, path, _remainGas, ref(code), ref(param)))
            {
                if (!executive->go())
                { // FIXME: shoud judge success use status, go always return true
                    PRECOMPILED_LOG(ERROR)
                        << LOG_BADGE("DeployWasmPrecompiled") << LOG_DESC("executive->go error")
                        << LOG_KV("path", path);
                    // FIXME:  return error message in PrecompiledError
                    BOOST_THROW_EXCEPTION(protocol::PrecompiledError());
                }
            }
            else
            {
                PRECOMPILED_LOG(ERROR)
                    << LOG_BADGE("DeployWasmPrecompiled")
                    << LOG_DESC("executive->executeCreate error") << LOG_KV("path", path);
                BOOST_THROW_EXCEPTION(protocol::PrecompiledError());
            }
            // open parentDir and write subdir
            if (!setContractFile(_context, parentDir, contractName))
            {
                PRECOMPILED_LOG(WARNING)
                    << LOG_BADGE("DeployWasmPrecompiled")
                    << LOG_DESC("setContractFile in parentDir error") << LOG_KV("path", path);
                callResult->setExecResult(m_codec->encode(false));
            }
            else
            {
                PRECOMPILED_LOG(INFO)
                    << LOG_BADGE("DeployWasmPrecompiled")
                    << LOG_DESC("setContractFile in parentDir success") << LOG_KV("path", path);
                callResult->setExecResult(m_codec->encode(true));
                _context->getState()->setAbi(path, jsonABI);
            }
        }
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("FileSystemPrecompiled")
                               << LOG_DESC("call undefined function!");
    }
    gasPricer->updateMemUsed(callResult->m_execResult.size());
    _remainGas -= gasPricer->calTotalGas();
    return callResult;
}

bool DeployWasmPrecompiled::setContractFile(std::shared_ptr<executor::BlockContext> _context,
    const std::string& _parentDir, const std::string& _contractName)
{
    auto parentTable = _context->getTableFactory()->openTable(_parentDir);
    if (!parentTable)
        return false;
    auto sub = parentTable->getRow(FS_KEY_SUB)->getField(SYS_VALUE);
    FileInfo newFile(_contractName, FS_TYPE_CONTRACT, _context->currentNumber());
    DirInfo parentDif;
    if (!DirInfo::fromString(parentDif, sub))
        return false;
    parentDif.getMutableSubDir().emplace_back(newFile);
    auto newEntry = parentTable->newEntry();
    newEntry->setField(SYS_VALUE, parentDif.toString());
    parentTable->setRow(FS_KEY_SUB, newEntry);
    return true;
}
