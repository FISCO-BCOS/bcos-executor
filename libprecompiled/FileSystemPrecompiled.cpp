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
#include <bcos-framework/interfaces/protocol/Exceptions.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/throw_exception.hpp>
#include <json/json.h>

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
    std::shared_ptr<executor::BlockContext> _context, bytesConstRef _param,
    const std::string&, const std::string&, u256& _remainGas)
{
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("KVTableFactory") << LOG_DESC("call")
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
                PRECOMPILED_LOG(TRACE) << LOG_BADGE("FileSystemPrecompiled")
                                       << LOG_DESC("directory exists");
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

bool FileSystemPrecompiled::recursiveBuildDir(
    const TableFactoryInterface::Ptr& _tableFactory, const std::string& _absoluteDir)
{
    if (_absoluteDir.empty())
    {
        return false;
    }
    auto dirList = std::make_shared<std::vector<std::string>>();
    std::string absoluteDir = _absoluteDir;
    if (absoluteDir[0] == '/')
    {
        absoluteDir = absoluteDir.substr(1);
    }
    if (absoluteDir.at(absoluteDir.size() - 1) == '/')
    {
        absoluteDir = absoluteDir.substr(0, absoluteDir.size() - 1);
    }
    boost::split(*dirList, absoluteDir, boost::is_any_of("/"), boost::token_compress_on);
    std::string root = "/";
    Json::Reader reader;
    Json::Value value, fsValue;
    Json::FastWriter fastWriter;
    for (auto& dir : *dirList)
    {
        auto table = _tableFactory->openTable(root);
        if (root != "/")
        {
            root += "/";
        }
        if (!table)
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("recursiveBuildDir")
                              << LOG_DESC("can not open table root") << LOG_KV("root", root);
            return false;
        }
        auto entry = table->getRow(FS_KEY_SUB);
        if (!entry)
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("recursiveBuildDir")
                              << LOG_DESC("can get entry of FS_KEY_SUB") << LOG_KV("root", root);
            return false;
        }
        auto subdirectories = entry->getField(SYS_VALUE);
        if (!reader.parse(subdirectories, value))
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("recursiveBuildDir") << LOG_DESC("parse json error")
                              << LOG_KV("jsonStr", subdirectories);
            return false;
        }
        fsValue["fileName"] = dir;
        fsValue["type"] = FS_TYPE_DIR;
        bool exist = false;
        for (const Json::Value& _v : value[FS_KEY_SUB])
        {
            if (_v["fileName"].asString() == dir)
            {
                exist = true;
                break;
            }
        }
        if (exist)
        {
            root += dir;
            continue;
        }
        value[FS_KEY_SUB].append(fsValue);
        entry->setField(SYS_VALUE, fastWriter.write(value));
        table->setRow(FS_KEY_SUB, entry);

        std::string newDir = root + dir;
        auto result = _tableFactory->createTable(newDir, SYS_KEY, SYS_VALUE);
        if(!result)
        {
            return false;
        }
        auto newTable = _tableFactory->openTable(newDir);
        auto typeEntry = newTable->newEntry();
        typeEntry->setField(SYS_VALUE, FS_TYPE_DIR);
        newTable->setRow(FS_KEY_TYPE, typeEntry);

        auto subEntry = newTable->newEntry();
        subEntry->setField(SYS_VALUE, "{}");
        newTable->setRow(FS_KEY_SUB, subEntry);
        root += dir;
    }
    return true;
}
