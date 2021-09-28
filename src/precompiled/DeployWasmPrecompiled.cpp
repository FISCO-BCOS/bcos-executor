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
#include "../executive/TransactionExecutive.h"
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
    const std::string& _origin, const std::string&)
{
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("DeployWasmPrecompiled") << LOG_DESC("call")
                           << LOG_KV("func", func);
    auto codec = std::make_shared<PrecompiledCodec>(_context->hashHandler(), _context->isWasm());
    auto callResult = std::make_shared<PrecompiledExecResult>();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();
    gasPricer->setMemUsed(_param.size());
    if (func == name2Selector[WASM_METHOD_DEPLOY])
    {
        bytes code, param;
        std::string path, jsonABI;
        codec->decode(data, code, param, path, jsonABI);
        auto table = _context->storage()->openTable(path);
        if (table)
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("DeployWasmPrecompiled")
                                   << LOG_DESC("path exist, create error") << LOG_KV("path", path);
            BOOST_THROW_EXCEPTION(protocol::PrecompiledError());
        }
        else
        {
            if (!checkPathValid(path))
            {
                PRECOMPILED_LOG(ERROR) << LOG_BADGE("DeployWasmPrecompiled")
                                       << LOG_DESC("Invalid path") << LOG_KV("path", path);
                BOOST_THROW_EXCEPTION(protocol::PrecompiledError());
            }
            // build dir
            auto parentDirAndBaseName = getParentDirAndBaseName(path);
            std::string parentDir = parentDirAndBaseName.first;
            std::string contractName = parentDirAndBaseName.second;
            if (recursiveBuildDir(_context->storage(), parentDir))
            {
                // TODO: get critical domains in jsonABI

                // FIXME: change usage
                //                auto executive = std::make_shared<TransactionExecutive>(_context);
                //                if (!executive->executeCreate(
                //                        _sender, _origin, path, _remainGas, ref(code),
                //                        ref(param)))
                //                {
                //                    executive->go();
                //                    if (executive->status() != TransactionStatus::None)
                //                    {
                //                        PRECOMPILED_LOG(ERROR)
                //                            << LOG_BADGE("DeployWasmPrecompiled") <<
                //                            LOG_DESC("executive->go error")
                //                            << LOG_KV("path", path);
                //                        // FIXME:  return error message in PrecompiledError
                //                        BOOST_THROW_EXCEPTION(protocol::PrecompiledError());
                //                    }
                //                }
                //                else
                //                {
                //                    PRECOMPILED_LOG(ERROR)
                //                        << LOG_BADGE("DeployWasmPrecompiled")
                //                        << LOG_DESC("executive->executeCreate error") <<
                //                        LOG_KV("path", path);
                //                    BOOST_THROW_EXCEPTION(protocol::PrecompiledError());
                //                }

                // open parentDir and write subdir
                if (!setContractFile(_context, parentDir, contractName, _origin))
                {
                    PRECOMPILED_LOG(WARNING)
                        << LOG_BADGE("DeployWasmPrecompiled")
                        << LOG_DESC("setContractFile in parentDir error") << LOG_KV("path", path);
                    callResult->setExecResult(codec->encode(s256((int)CODE_FILE_SET_WASM_FAILED)));
                }
                else
                {
                    PRECOMPILED_LOG(INFO)
                        << LOG_BADGE("DeployWasmPrecompiled")
                        << LOG_DESC("setContractFile in parentDir success") << LOG_KV("path", path);
                    callResult->setExecResult(codec->encode(s256((int)CODE_SUCCESS)));
                    // FIXME: how to set abi?
                    //                    _context->getState()->setAbi(path, jsonABI);
                }
            }
            else
            {
                PRECOMPILED_LOG(ERROR)
                    << LOG_BADGE("DeployWasmPrecompiled") << LOG_DESC("recursive build dir failed")
                    << LOG_KV("path", path);
                callResult->setExecResult(codec->encode(s256((int)CODE_FILE_BUILD_DIR_FAILED)));
            }
        }
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("FileSystemPrecompiled")
                               << LOG_DESC("call undefined function!");
    }
    gasPricer->updateMemUsed(callResult->m_execResult.size());
    callResult->setGas(gasPricer->calTotalGas());
    return callResult;
}

bool DeployWasmPrecompiled::setContractFile(std::shared_ptr<executor::BlockContext> _context,
    const std::string& _parentDir, const std::string& _contractName, const std::string& _owner)
{
    auto parentTable = _context->storage()->openTable(_parentDir);
    if (!parentTable)
        return false;
    auto newEntry = parentTable->newEntry();
    newEntry.setField(FS_FIELD_TYPE, FS_TYPE_CONTRACT);
    // FIXME: consider permission inheritance
    newEntry.setField(FS_FIELD_ACCESS, "");
    newEntry.setField(FS_FIELD_OWNER, _owner);
    newEntry.setField(FS_FIELD_GID, "");
    newEntry.setField(FS_FIELD_EXTRA, "");
    parentTable->setRow(_contractName, newEntry);
    return true;
}

bool DeployWasmPrecompiled::checkPathValid(std::string const& _path)
{
    if (_path.length() > FS_PATH_MAX_LENGTH)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("checkPathValid") << LOG_DESC("path too long")
                               << LOG_KV("path", _path);
        return false;
    }
    std::string absoluteDir = _path;
    if (absoluteDir[0] == '/')
    {
        absoluteDir = absoluteDir.substr(1);
    }
    if (absoluteDir.at(absoluteDir.size() - 1) == '/')
    {
        absoluteDir = absoluteDir.substr(0, absoluteDir.size() - 1);
    }
    std::vector<std::string> pathList;
    boost::split(pathList, absoluteDir, boost::is_any_of("/"), boost::token_compress_on);
    if (pathList.size() > FS_PATH_MAX_LEVEL || pathList.empty())
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("checkPathValid")
                               << LOG_DESC("resource path's level is too deep")
                               << LOG_KV("path", _path);
        return false;
    }
    std::vector<char> allowChar = {'_'};
    auto checkFieldNameValidate = [&allowChar](const std::string& fieldName) -> bool {
        if (fieldName.empty() || fieldName[0] == '_')
        {
            std::stringstream errorMessage;
            errorMessage << "Invalid field \"" + fieldName
                         << "\", the size of the field must be larger than 0 and "
                            "the field can't start with \"_\"";
            STORAGE_LOG(ERROR) << LOG_DESC(errorMessage.str()) << LOG_KV("field name", fieldName);
            return false;
        }
        for (size_t i = 0; i < fieldName.size(); i++)
        {
            if (!isalnum(fieldName[i]) &&
                (allowChar.end() == find(allowChar.begin(), allowChar.end(), fieldName[i])))
            {
                std::stringstream errorMessage;
                errorMessage << "Invalid field \"" << fieldName
                             << "\", the field name must be letters or numbers.";
                STORAGE_LOG(ERROR)
                    << LOG_DESC(errorMessage.str()) << LOG_KV("field name", fieldName);
                return false;
            }
        }
        return true;
    };
    for (const auto& path : pathList)
    {
        if (!checkFieldNameValidate(path))
        {
            return false;
        }
    }
    return true;
}
