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
 * @file Utilities.cpp
 * @author: kyonRay
 * @date 2021-05-25
 */

#include "Utilities.h"
#include "../libstate/State.h"
#include "Common.h"
#include <bcos-framework/interfaces/crypto/Hash.h>
#include <json/json.h>
#include <tbb/concurrent_unordered_map.h>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::protocol;
using namespace bcos::crypto;

static tbb::concurrent_unordered_map<std::string, uint32_t> s_name2SelectCache;

void bcos::precompiled::checkNameValidate(const std::string& tableName,
    std::vector<std::string>& keyFieldList, std::vector<std::string>& valueFieldList)
{
    std::set<std::string> valueFieldSet;
    std::set<std::string> keyFieldSet;
    std::vector<char> allowChar = {'$', '_', '@'};
    std::vector<char> tableAllowChar = {'$', '_', '@', '/'};
    std::string allowCharString = "{$, _, @}";
    std::string tableAllowCharString = "{$, _, @, /}";
    auto checkTableNameValidate = [&tableAllowChar, &tableAllowCharString](
                                      const std::string& tableName) {
        size_t iSize = tableName.size();
        for (size_t i = 0; i < iSize; i++)
        {
            if (!isalnum(tableName[i]) &&
                (tableAllowChar.end() ==
                    find(tableAllowChar.begin(), tableAllowChar.end(), tableName[i])))
            {
                std::stringstream errorMsg;
                errorMsg << "Invalid table name \"" << tableName
                         << "\", the table name must be letters or numbers, and "
                            "only supports \""
                         << tableAllowCharString << "\" as special character set";
                STORAGE_LOG(ERROR) << LOG_DESC(errorMsg.str());
                // Note: the StorageException and PrecompiledException content can't
                // be modified at will for the information will be write to the
                // blockchain
                BOOST_THROW_EXCEPTION(
                    PrecompiledError() << errinfo_comment("invalid table name:" + tableName));
            }
        }
    };

    auto checkFieldNameValidate = [&allowChar, &allowCharString](
                                      const std::string& tableName, const std::string& fieldName) {
        if (fieldName.empty() || fieldName[0] == '_')
        {
            std::stringstream errorMessage;
            errorMessage << "Invalid field \"" + fieldName
                         << "\", the size of the field must be larger than 0 and "
                            "the field can't start with \"_\"";
            STORAGE_LOG(ERROR) << LOG_DESC(errorMessage.str()) << LOG_KV("field name", fieldName)
                               << LOG_KV("table name", tableName);
            BOOST_THROW_EXCEPTION(
                PrecompiledError() << errinfo_comment("invalid field: " + fieldName));
        }
        size_t iSize = fieldName.size();
        for (size_t i = 0; i < iSize; i++)
        {
            if (!isalnum(fieldName[i]) &&
                (allowChar.end() == find(allowChar.begin(), allowChar.end(), fieldName[i])))
            {
                std::stringstream errorMessage;
                errorMessage
                    << "Invalid field \"" << fieldName
                    << "\", the field name must be letters or numbers, and only supports \""
                    << allowCharString << "\" as special character set";

                STORAGE_LOG(ERROR)
                    << LOG_DESC(errorMessage.str()) << LOG_KV("field name", fieldName)
                    << LOG_KV("table name", tableName);
                BOOST_THROW_EXCEPTION(
                    PrecompiledError() << errinfo_comment("invalid filed: " + fieldName));
            }
        }
    };

    checkTableNameValidate(tableName);

    for (auto& keyField : keyFieldList)
    {
        auto ret = keyFieldSet.insert(keyField);
        if (!ret.second)
        {
            PRECOMPILED_LOG(ERROR) << LOG_DESC("duplicated key") << LOG_KV("key name", keyField)
                                   << LOG_KV("table name", tableName);
            BOOST_THROW_EXCEPTION(
                PrecompiledError() << errinfo_comment("duplicated key: " + keyField));
        }
        checkFieldNameValidate(tableName, keyField);
    }

    for (auto& valueField : valueFieldList)
    {
        auto ret = valueFieldSet.insert(valueField);
        if (!ret.second)
        {
            PRECOMPILED_LOG(ERROR)
                << LOG_DESC("duplicated field") << LOG_KV("field name", valueField)
                << LOG_KV("table name", tableName);
            BOOST_THROW_EXCEPTION(
                PrecompiledError() << errinfo_comment("duplicated field: " + valueField));
        }
        checkFieldNameValidate(tableName, valueField);
    }
}

int bcos::precompiled::checkLengthValidate(
    const std::string& fieldValue, int32_t maxLength, int32_t errorCode)
{
    if (fieldValue.size() > (size_t)maxLength)
    {
        PRECOMPILED_LOG(ERROR) << "key:" << fieldValue << " value size:" << fieldValue.size()
                               << " greater than " << maxLength;
        BOOST_THROW_EXCEPTION(PrecompiledError()
                              << errinfo_comment(
                                     "size of value/key greater than" + std::to_string(maxLength))
                              << errinfo_comment(std::to_string(errorCode)));
        return errorCode;
    }
    return 0;
}
uint32_t bcos::precompiled::getFuncSelector(
    std::string const& _functionName, const crypto::Hash::Ptr& _hashImpl)
{
    // global function selector cache
    if (s_name2SelectCache.count(_functionName))
    {
        return s_name2SelectCache[_functionName];
    }
    auto selector = getFuncSelectorByFunctionName(_functionName, _hashImpl);
    s_name2SelectCache.insert(std::make_pair(_functionName, selector));
    return selector;
}

uint32_t bcos::precompiled::getParamFunc(bytesConstRef _param)
{
    auto funcBytes = _param.getCroppedData(0, 4);
    uint32_t func = *((uint32_t*)(funcBytes.data()));

    return ((func & 0x000000FF) << 24) | ((func & 0x0000FF00) << 8) | ((func & 0x00FF0000) >> 8) |
           ((func & 0xFF000000) >> 24);
}

bytesConstRef bcos::precompiled::getParamData(bytesConstRef _param)
{
    return _param.getCroppedData(4);
}

uint32_t bcos::precompiled::getFuncSelectorByFunctionName(
    std::string const& _functionName, const crypto::Hash::Ptr& _hashImpl)
{
    uint32_t func = *(uint32_t*)(_hashImpl->hash(_functionName).ref().getCroppedData(0, 4).data());
    uint32_t selector = ((func & 0x000000FF) << 24) | ((func & 0x0000FF00) << 8) |
                        ((func & 0x00FF0000) >> 8) | ((func & 0xFF000000) >> 24);
    return selector;
}

bcos::precompiled::ContractStatus bcos::precompiled::getContractStatus(
    std::shared_ptr<bcos::executor::BlockContext> _context, const std::string& _tableName)
{
    auto table = _context->getTableFactory()->openTable(_tableName);
    if (!table)
    {
        return ContractStatus::AddressNonExistent;
    }

    auto codeHashEntry = table->getRow(executor::ACCOUNT_CODE_HASH);
    HashType codeHash;
    codeHash = HashType(codeHashEntry->getField(executor::STORAGE_VALUE));

    if (codeHash == HashType(""))
    {
        return ContractStatus::NotContractAddress;
    }

    auto frozenEntry = table->getRow(executor::ACCOUNT_FROZEN);
    if ("true" == frozenEntry->getField(executor::STORAGE_VALUE))
    {
        return ContractStatus::Frozen;
    }
    else
    {
        return ContractStatus::Available;
    }
    PRECOMPILED_LOG(ERROR) << LOG_DESC("getContractStatus error")
                           << LOG_KV("table name", _tableName);
}

void Condition::EQ(const std::string& key, const std::string& value)
{
    addCondition(key, value, m_conditions, Comparator::EQ);
}

void Condition::NE(const std::string& key, const std::string& value)
{
    // not equal contains two area
    addCondition(key, value, m_conditions, Comparator::NE);
}

void Condition::GT(const std::string& key, const std::string& value)
{
    addCondition(key, value, m_conditions, Comparator::GT);
}

void Condition::GE(const std::string& key, const std::string& value)
{
    addCondition(key, value, m_conditions, Comparator::GE);
}

void Condition::LT(const std::string& key, const std::string& value)
{
    addCondition(key, value, m_conditions, Comparator::LT);
}

void Condition::LE(const std::string& key, const std::string& value)
{
    addCondition(key, value, m_conditions, Comparator::LE);
}

bool Condition::filter(storage::Entry::Ptr _entry)
{
    if (_entry == nullptr)
    {
        return false;
    }
    if (_entry->getStatus() == storage::Entry::Status::DELETED)
    {
        return false;
    }
    try
    {
        if (!m_conditions.empty())
        {
            for (auto& condition : m_conditions)
            {
                auto fieldIt = _entry->find(condition.left);
                if (fieldIt != _entry->end())
                {
                    switch (condition.cmp)
                    {
                    case Comparator::EQ:
                    {
                        if (fieldIt->second != condition.right)
                        {
                            return false;
                        }
                        break;
                    }
                    case Comparator::NE:
                    {
                        if (fieldIt->second == condition.right)
                        {
                            return false;
                        }
                        break;
                    }
                    case Comparator::GT:
                    {
                        int64_t lhs = INT64_MIN;
                        auto rhs = boost::lexical_cast<int64_t>(condition.right);
                        if (!fieldIt->second.empty())
                        {
                            lhs = boost::lexical_cast<int64_t>(fieldIt->second);
                        }
                        if (lhs <= rhs)
                        {
                            return false;
                        }
                        break;
                    }
                    case Comparator::GE:
                    {
                        int64_t lhs = INT64_MIN;
                        auto rhs = boost::lexical_cast<int64_t>(condition.right);
                        if (!fieldIt->second.empty())
                        {
                            lhs = boost::lexical_cast<int64_t>(fieldIt->second);
                        }
                        if (lhs < rhs)
                        {
                            return false;
                        }
                        break;
                    }
                    case Comparator::LT:
                    {
                        int64_t lhs = INT64_MAX;
                        auto rhs = boost::lexical_cast<int64_t>(condition.right);
                        if (!fieldIt->second.empty())
                        {
                            lhs = boost::lexical_cast<int64_t>(fieldIt->second);
                        }
                        if (lhs >= rhs)
                        {
                            return false;
                        }
                        break;
                    }
                    case Comparator::LE:
                    {
                        int64_t lhs = INT64_MAX;
                        auto rhs = boost::lexical_cast<int64_t>(condition.right);
                        if (!fieldIt->second.empty())
                        {
                            lhs = boost::lexical_cast<int64_t>(fieldIt->second);
                        }
                        if (lhs > rhs)
                        {
                            return false;
                        }
                        break;
                    }
                    default:
                    {
                    }
                    }
                }
            }
        }
    }
    catch (...)
    {
        return false;
    }
    return true;
}

void Condition::limit(size_t count)
{
    limit(0, count);
}

void Condition::limit(size_t start, size_t end)
{
    m_limit = {start, end};
}

void precompiled::transferKeyCond(CompareTriple& _entryCond, storage::Condition::Ptr& _keyCond)
{
    switch (_entryCond.cmp)
    {
    case Comparator::EQ:
        break;
    case Comparator::NE:
        _keyCond->NE(_entryCond.right);
        break;
    case Comparator::GT:
        _keyCond->GT(_entryCond.right);
        break;
    case Comparator::GE:
        _keyCond->GE(_entryCond.right);
        break;
    case Comparator::LT:
        _keyCond->LT(_entryCond.right);
        break;
    case Comparator::LE:
        _keyCond->LE(_entryCond.right);
        break;
    }
}

void precompiled::addCondition(const std::string& key, const std::string& value,
    std::vector<CompareTriple>& _cond, Comparator _cmp)
{
    auto it = std::find_if(
        _cond.begin(), _cond.end(), [key, value, _cmp](const CompareTriple& item) -> bool {
            return item.left == key && item.right == value && item.cmp == _cmp;
        });
    // duplicate removal
    // TODO: range narrowing
    if (it == _cond.end())
    {
        _cond.emplace_back(CompareTriple(key, value, _cmp));
    }
}

uint64_t precompiled::getEntriesCapacity(precompiled::EntriesConstPtr _entries)
{
    int64_t totalCapacity = 0;
    int64_t entriesSize = _entries->size();
    for (int64_t i = 0; i < entriesSize; i++)
    {
        totalCapacity += _entries->at(i)->capacityOfHashField();
    }
    return totalCapacity;
}

bool precompiled::checkPathValid(std::string const& _path)
{
    if (_path.length() > FS_PATH_MAX_LENGTH)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("checkPathValid") << LOG_DESC("path too long")
                               << LOG_KV("path", _path);
        return false;
    }
    if (_path == "/")
        return true;
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
    return std::all_of(pathList.begin(), pathList.end(),
        [checkFieldNameValidate](const std::string& s) { return checkFieldNameValidate(s); });
}

/// /usr/test => /usr
std::string precompiled::getParentDir(const std::string& _absolutePath)
{
    if (_absolutePath == "/" || _absolutePath.empty())
    {
        return _absolutePath;
    }
    // /usr/test/ => /usr/test
    std::string ret = (_absolutePath.at(_absolutePath.length() - 1) == '/') ?
                          _absolutePath.substr(0, _absolutePath.find_last_of('/') - 1) :
                          _absolutePath;
    if (_absolutePath.find_last_of('/') == 0)
    {
        return "/";
    }
    return ret.substr(0, _absolutePath.find_last_of('/'));
}

/// /usr/test => test
std::string precompiled::getDirBaseName(const std::string& _absolutePath)
{
    if (_absolutePath == "/" || _absolutePath.empty())
    {
        return _absolutePath;
    }
    // /usr/test/ => /usr/test
    std::string ret = (_absolutePath.at(_absolutePath.length() - 1) == '/') ?
                          _absolutePath.substr(0, _absolutePath.find_last_of('/') - 1) :
                          _absolutePath;
    return ret.substr(_absolutePath.find_last_of('/') + 1);
}

bool precompiled::recursiveBuildDir(
    const storage::TableFactoryInterface::Ptr& _tableFactory, const std::string& _absoluteDir)
{
    if (_absoluteDir.empty())
    {
        return false;
    }
    // transfer /usr/local/bin => ["usr", "local", "bin"]
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
    for (auto& dir : *dirList)
    {
        auto table = _tableFactory->openTable(root);
        if (!table)
        {
            PRECOMPILED_LOG(ERROR)
                << LOG_BADGE("recursiveBuildDir") << LOG_DESC("can not open path table")
                << LOG_KV("tableName", root);
            return false;
        }
        if (root != "/")
        {
            root += "/";
        }
        auto entry = table->getRow(dir);
        if (entry)
        {
            if (entry->getField(FS_FIELD_TYPE) != FS_TYPE_DIR)
            {
                PRECOMPILED_LOG(ERROR)
                    << LOG_BADGE("recursiveBuildDir")
                    << LOG_DESC("file had already existed, and not directory type")
                    << LOG_KV("parentDir", root) << LOG_KV("dir", dir);
                return false;
            }
            PRECOMPILED_LOG(DEBUG) << LOG_BADGE("recursiveBuildDir")
                                   << LOG_DESC("dir already existed in parent dir, continue")
                                   << LOG_KV("parentDir", root) << LOG_KV("dir", dir);
            root += dir;
            continue;
        }
        // not exist, then create table and write in parent dir
        auto newFileEntry = table->newEntry();
        newFileEntry->setField(FS_FIELD_TYPE, FS_TYPE_DIR);
        // FIXME: consider permission inheritance
        newFileEntry->setField(FS_FIELD_ACCESS, "");
        newFileEntry->setField(FS_FIELD_OWNER, "");
        newFileEntry->setField(FS_FIELD_GID, "");
        newFileEntry->setField(FS_FIELD_EXTRA, "");
        table->setRow(dir, newFileEntry);

        _tableFactory->createTable(root + dir, FS_KEY_NAME, FS_FIELD_COMBINED);
        root += dir;
    }
    return true;
}