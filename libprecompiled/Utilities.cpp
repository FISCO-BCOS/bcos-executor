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
#include <tbb/concurrent_unordered_map.h>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::protocol;
using namespace bcos::crypto;

static tbb::concurrent_unordered_map<std::string, uint32_t> s_name2SelectCache;

void bcos::precompiled::checkNameValidate(
        const std::string &tableName, std::string &keyField,
        std::vector<std::string> &valueFieldList) {

    std::set<std::string> valueFieldSet;
    boost::trim(keyField);
    valueFieldSet.insert(keyField);
    std::vector<char> allowChar = {'$', '_', '@'};
    std::string allowCharString = "{$, _, @}";
    auto checkTableNameValidate =
            [&allowChar, &allowCharString](const std::string &tableName) {
                size_t iSize = tableName.size();
                for (size_t i = 0; i < iSize; i++) {
                    if (!isalnum(tableName[i]) &&
                        (allowChar.end() ==
                         find(allowChar.begin(), allowChar.end(), tableName[i]))) {
                        std::stringstream errorMsg;
                        errorMsg << "Invalid table name \"" << tableName
                                 << "\", the table name must be letters or numbers, and "
                                    "only supports \""
                                 << allowCharString << "\" as special character set";
                        STORAGE_LOG(ERROR) << LOG_DESC(errorMsg.str());
                        // Note: the StorageException and PrecompiledException content can't
                        // be modified at will for the information will be write to the
                        // blockchain
                        BOOST_THROW_EXCEPTION(PrecompiledError() << errinfo_comment(
                                                      "invalid table name:" + tableName));
                    }
                }
            };

    auto checkFieldNameValidate =
            [allowChar, allowCharString](
                    const std::string &tableName, const std::string &fieldName) {
                if (fieldName.empty() || fieldName[0] == '_') {
                    std::stringstream errorMessage;
                    errorMessage << "Invalid field \"" + fieldName
                                 << "\", the size of the field must be larger than 0 and "
                                    "the field can't start with \"_\"";
                    STORAGE_LOG(ERROR)
                            << LOG_DESC(errorMessage.str()) << LOG_KV("field name", fieldName)
                            << LOG_KV("table name", tableName);
                    BOOST_THROW_EXCEPTION(
                            PrecompiledError() << errinfo_comment("invalid field: " + fieldName));
                }
                size_t iSize = fieldName.size();
                for (size_t i = 0; i < iSize; i++) {
                    if (!isalnum(fieldName[i]) &&
                        (allowChar.end() ==
                         find(allowChar.begin(), allowChar.end(), fieldName[i]))) {
                        std::stringstream errorMessage;
                        errorMessage << "Invalid field \"" << fieldName << "\", the field name must be letters or numbers, and only supports \"" << allowCharString << "\" as special character set";

                        STORAGE_LOG(ERROR) << LOG_DESC(errorMessage.str())
                                           << LOG_KV("field name", fieldName)
                                           << LOG_KV("table name", tableName);
                        BOOST_THROW_EXCEPTION(
                                PrecompiledError() << errinfo_comment("invalid filed: " + fieldName));
                    }
                }
            };

    checkTableNameValidate(tableName);
    checkFieldNameValidate(tableName, keyField);

    for (auto &valueField : valueFieldList) {
        auto ret = valueFieldSet.insert(valueField);
        if (!ret.second) {
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
                          << errinfo_comment ("size of value/key greater than" + std::to_string(maxLength))
                          << errinfo_comment(std::to_string(errorCode)));

    return errorCode;
  }
  return 0;
}
uint32_t bcos::precompiled::getFuncSelector(std::string const& _functionName)
{
    // global function selector cache
    if (s_name2SelectCache.count(_functionName))
    {
        return s_name2SelectCache[_functionName];
    }
    auto selector = getFuncSelectorByFunctionName(_functionName);
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

uint32_t bcos::precompiled::getFuncSelectorByFunctionName(std::string const& _functionName)
{
  uint32_t func = *(uint32_t*)(crypto::HashType (_functionName).ref().getCroppedData(0, 4).data());
  uint32_t selector = ((func & 0x000000FF) << 24) | ((func & 0x0000FF00) << 8) |
                      ((func & 0x00FF0000) >> 8) | ((func & 0xFF000000) >> 24);
  return selector;
}

bcos::precompiled::ContractStatus bcos::precompiled::getContractStatus(
    std::shared_ptr<bcos::executor::ExecutiveContext> _context, const std::string& _tableName)
{
    auto table = _context->getTableFactory()->openTable(_tableName);
    if (!table)
    {
        return ContractStatus::AddressNonExistent;
    }

    auto codeHashEntry = table->getRow(executor::ACCOUNT_CODE_HASH);
    h256 codeHash;
    codeHash = h256(*fromHexString(codeHashEntry->getField(executor::STORAGE_VALUE)));

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
    return ContractStatus::Invalid;
}
