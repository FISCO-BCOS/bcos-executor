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
#include <json/json.h>
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

    if (func == name2Selector[FILE_SYSTEM_METHOD_LIST])
    {
        // list(string)
        listDir(_context, data, callResult, gasPricer);
    }
    else if (func == name2Selector[FILE_SYSTEM_METHOD_MKDIR])
    {
        // mkdir(string)
        makeDir(_context, data, callResult, gasPricer);
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
void FileSystemPrecompiled::makeDir(const std::shared_ptr<executor::BlockContext>& _context,
    bytesConstRef& data, std::shared_ptr<PrecompiledExecResult> callResult,
    const PrecompiledGas::Ptr& gasPricer)
{
    // mkdir(string)
    std::string absolutePath;
    this->m_codec->decode(data, absolutePath);
    auto table = _context->getTableFactory()->openTable(absolutePath);
    gasPricer->appendOperation(InterfaceOpcode::OpenTable);
    if (table)
    {
        auto type = table->getRow(FS_KEY_TYPE)->getField(SYS_VALUE);
        if (type == "directory")
        {
            PRECOMPILED_LOG(TRACE)
                << LOG_BADGE("FileSystemPrecompiled") << LOG_DESC("directory exists");
            callResult->setExecResult(this->m_codec->encode(true));
        }
        else
        {
            // regular file
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("FileSystemPrecompiled")
                                   << LOG_DESC("file name exists, not a directory");
            callResult->setExecResult(this->m_codec->encode(false));
        }
    }
    else
    {
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("FileSystemPrecompiled")
                               << LOG_DESC("directory not exists") << LOG_KV("path", absolutePath);
        auto result = recursiveBuildDir(_context->getTableFactory(), absolutePath);
        callResult->setExecResult(this->m_codec->encode(result));
    }
}

void FileSystemPrecompiled::listDir(const std::shared_ptr<executor::BlockContext>& _context,
    bytesConstRef& data, std::shared_ptr<PrecompiledExecResult> callResult,
    const PrecompiledGas::Ptr& gasPricer)
{
    // list(string)
    std::string absolutePath;
    this->m_codec->decode(data, absolutePath);
    auto table = _context->getTableFactory()->openTable(absolutePath);
    gasPricer->appendOperation(InterfaceOpcode::OpenTable);

    if (table)
    {
        auto type = table->getRow(FS_KEY_TYPE)->getField(SYS_VALUE);
        if (type == "directory")
        {
            Json::Value directory;
            Json::Value subdirectory(Json::arrayValue);
            auto subdirectories = table->getRow(FS_KEY_SUB)->getField(SYS_VALUE);
            DirInfo dirInfo;
            DirInfo::fromString(dirInfo, subdirectories);
            for (auto& fileInfo : dirInfo.getSubDir())
            {
                Json::Value file;
                file["type"] = fileInfo.getType();
                file["name"] = fileInfo.getName();
                file["enable_number"] = fileInfo.getNumber();
                subdirectory.append(file);
            }
            directory[FS_KEY_TYPE] = type;
            directory[FS_KEY_SUB] = subdirectory;
            directory[FS_KEY_NUM] = table->getRow(FS_KEY_NUM)->getField(SYS_VALUE);

            Json::FastWriter fastWriter;
            std::string str = fastWriter.write(directory);
            PRECOMPILED_LOG(TRACE)
                << LOG_BADGE("FileSystemPrecompiled") << LOG_DESC("ls dir, return subdirectories");
            callResult->setExecResult(this->m_codec->encode(str));
        }
        else
        {
            // regular file
            Json::Value file;
            file[FS_KEY_TYPE] = type;
            file[FS_KEY_NUM] = table->getRow(FS_KEY_NUM)->getField(SYS_VALUE);
            Json::FastWriter fastWriter;
            std::string str = fastWriter.write(file);
            // TODO: add permission mod when permission support
            callResult->setExecResult(this->m_codec->encode(str));
        }
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("FileSystemPrecompiled")
                               << LOG_DESC("can't open table of file path")
                               << LOG_KV("path", absolutePath);
        getErrorCodeOut(callResult->mutableExecResult(), CODE_FILE_NOT_EXIST, this->m_codec);
    }
}
