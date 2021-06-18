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
 * @file FileSystemPrecompiled.cpp
 * @author: kyonRay
 * @date 2021-06-10
 */

#include "FileSystemPrecompiled.h"
#include "Common.h"
#include "PrecompiledResult.h"
#include "Utilities.h"
#include <boost/algorithm/string/split.hpp>
#include <boost/throw_exception.hpp>

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::precompiled;
using namespace bcos::protocol;

const char* const FILE_SYSTEM_METHOD_LIST = "list(string)";
const char* const FILE_SYSTEM_METHOD_MKDIR = "mkdir(string)";

FileSystemPrecompiled::FileSystemPrecompiled(crypto::Hash::Ptr _hashImpl) : Precompiled(_hashImpl)
{
    name2Selector[FILE_SYSTEM_METHOD_LIST] = getFuncSelector(FILE_SYSTEM_METHOD_LIST, _hashImpl);
    name2Selector[FILE_SYSTEM_METHOD_MKDIR] = getFuncSelector(FILE_SYSTEM_METHOD_MKDIR, _hashImpl);
}

std::string FileSystemPrecompiled::toString()
{
    return "FileSystem";
}

std::shared_ptr<PrecompiledExecResult> FileSystemPrecompiled::call(
    std::shared_ptr<executor::BlockContext> _context, bytesConstRef _param, const std::string&,
    const std::string&, u256& _remainGas)
{
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("FileSystemPrecompiled") << LOG_DESC("call")
                           << LOG_KV("func", func);

    m_codec = std::make_shared<PrecompiledCodec>(_context->hashHandler(), _context->isWasm());
    auto callResult = std::make_shared<PrecompiledExecResult>();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();
    gasPricer->setMemUsed(_param.size());

    // FIXME: list return json string
    if (func == name2Selector[FILE_SYSTEM_METHOD_LIST])
    {
        // list(string)
        std::string absolutePath;
        m_codec->decode(data, absolutePath);
        auto table = _context->getTableFactory()->openTable(absolutePath);
        gasPricer->appendOperation(InterfaceOpcode::OpenTable);

        if (table)
        {
            auto type = table->getRow(FS_KEY_TYPE)->getField(SYS_VALUE);
            if (type == "directory")
            {
                auto subdirectories = table->getRow(FS_KEY_SUB)->getField(SYS_VALUE);
                PRECOMPILED_LOG(TRACE) << LOG_BADGE("FileSystemPrecompiled")
                                       << LOG_DESC("ls dir, return subdirectories")
                                       << LOG_KV("subdirectories", subdirectories);
                callResult->setExecResult(m_codec->encode(type, subdirectories));
            }
            else
            {
                // regular file
                // TODO: add permission mod when permission support
                callResult->setExecResult(m_codec->encode(type));
            }
        }
        else
        {
            PRECOMPILED_LOG(ERROR)
                << LOG_BADGE("FileSystemPrecompiled") << LOG_DESC("can't open table of file path")
                << LOG_KV("path", absolutePath);
            callResult->setExecResult(m_codec->encode((int)CODE_FILE_NOT_EXIST));
        }
    }
    else if (func == name2Selector[FILE_SYSTEM_METHOD_MKDIR])
    {
        // mkdir(string)
        std::string absolutePath;
        m_codec->decode(data, absolutePath);
        auto table = _context->getTableFactory()->openTable(absolutePath);
        gasPricer->appendOperation(InterfaceOpcode::OpenTable);
        if (table)
        {
            auto type = table->getRow(FS_KEY_TYPE)->getField(SYS_VALUE);
            if (type == "directory")
            {
                PRECOMPILED_LOG(TRACE)
                    << LOG_BADGE("FileSystemPrecompiled") << LOG_DESC("directory exists");
                callResult->setExecResult(m_codec->encode(true));
            }
            else
            {
                // regular file
                PRECOMPILED_LOG(ERROR) << LOG_BADGE("FileSystemPrecompiled")
                                       << LOG_DESC("file name exists, not a directory");
                callResult->setExecResult(m_codec->encode(false));
            }
        }
        else
        {
            PRECOMPILED_LOG(TRACE)
                << LOG_BADGE("FileSystemPrecompiled") << LOG_DESC("directory not exists")
                << LOG_KV("path", absolutePath);
            auto result = recursiveBuildDir(_context->getTableFactory(), absolutePath);
            callResult->setExecResult(m_codec->encode(result));
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
