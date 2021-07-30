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
 * @file Utilities.h
 * @author: kyonRay
 * @date 2021-05-25
 */

#pragma once

#include "../libvm/BlockContext.h"
#include "Common.h"
#include "PrecompiledCodec.h"
#include <bcos-framework/interfaces/storage/TableInterface.h>
#include <bcos-framework/libcodec/abi/ContractABICodec.h>
#include <bcos-framework/libutilities/Common.h>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/vector.hpp>

namespace bcos
{
namespace precompiled
{
static const std::string USER_TABLE_PREFIX = "u_";
static const std::string USER_TABLE_PREFIX_WASM = "/data";

enum class Comparator
{
    EQ,
    NE,
    GT,
    GE,
    LT,
    LE,
};
struct CompareTriple
{
    using Ptr = std::shared_ptr<Comparator>;
    CompareTriple(const std::string& _left, const std::string& _right, Comparator _cmp)
      : left(_left), right(_right), cmp(_cmp){};

    std::string left;
    std::string right;
    Comparator cmp;
};
struct Condition : public std::enable_shared_from_this<Condition>
{
    using Ptr = std::shared_ptr<Condition>;
    Condition() = default;
    void EQ(const std::string& key, const std::string& value);
    void NE(const std::string& key, const std::string& value);

    void GT(const std::string& key, const std::string& value);
    void GE(const std::string& key, const std::string& value);

    void LT(const std::string& key, const std::string& value);
    void LE(const std::string& key, const std::string& value);

    void limit(size_t count);
    void limit(size_t start, size_t end);

    bool filter(storage::Entry::Ptr entry);
    std::vector<CompareTriple> m_conditions;
    std::pair<size_t, size_t> m_limit;
};

class FileInfo
{
public:
    FileInfo() = default;
    FileInfo(const std::string& name, const std::string& type, protocol::BlockNumber number)
      : name(name), type(type), number(number)
    {}
    const std::string& getName() const { return name; }
    const std::string& getType() const { return type; }
    protocol::BlockNumber getNumber() const { return number; }

private:
    friend class boost::serialization::access;
    template <typename Archive>
    void serialize(Archive& ar, const unsigned int)
    {
        ar& name;
        ar& type;
        ar& number;
    }
    std::string name;
    std::string type;
    protocol::BlockNumber number;
};

class DirInfo
{
public:
    DirInfo() = default;
    explicit DirInfo(const std::vector<FileInfo>& subDir) : subDir(subDir) {}
    const std::vector<FileInfo>& getSubDir() const { return subDir; }
    std::vector<FileInfo>& getMutableSubDir() { return subDir; }
    std::string toString();
    static bool fromString(DirInfo& _dir, std::string _str);
    static std::string emptyDirString()
    {
        DirInfo emptyDir;
        return emptyDir.toString();
    }

private:
    friend class boost::serialization::access;
    template <typename Archive>
    void serialize(Archive& ar, const unsigned int)
    {
        ar& subDir;
    }
    std::vector<FileInfo> subDir;
};

void addCondition(const std::string& key, const std::string& value,
    std::vector<CompareTriple>& _cond, Comparator _cmp);

void transferKeyCond(CompareTriple& _entryCond, storage::Condition::Ptr& _keyCond);

inline void getErrorCodeOut(bytes& out, int const& result, const PrecompiledCodec::Ptr& _codec)
{
    if (result >= 0 && result < 128)
    {
        out = _codec->encode(u256(result));
        return;
    }
    out = _codec->encode(s256(result));
}
inline std::string getTableName(const std::string& _tableName, bool _isWasm)
{
    return _isWasm ? USER_TABLE_PREFIX + USER_TABLE_PREFIX_WASM + _tableName :
                     USER_TABLE_PREFIX + _tableName;
}

void checkNameValidate(const std::string& tableName, std::vector<std::string>& keyFieldList,
    std::vector<std::string>& valueFieldList);
int checkLengthValidate(const std::string& field_value, int32_t max_length, int32_t errorCode);

uint32_t getFuncSelector(std::string const& _functionName, const crypto::Hash::Ptr& _hashImpl);
uint32_t getParamFunc(bytesConstRef _param);
uint32_t getFuncSelectorByFunctionName(
    std::string const& _functionName, const crypto::Hash::Ptr& _hashImpl);

bcos::precompiled::ContractStatus getContractStatus(
    std::shared_ptr<bcos::executor::BlockContext> _context, std::string const& _tableName);

bytesConstRef getParamData(bytesConstRef _param);

uint64_t getEntriesCapacity(precompiled::EntriesConstPtr _entries);

void sortKeyValue(std::vector<std::string>& _v);

bool recursiveBuildDir(
    const storage::TableFactoryInterface::Ptr& _tableFactory, const std::string& _absoluteDir);
}  // namespace precompiled
}  // namespace bcos