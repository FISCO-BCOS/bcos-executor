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

#include "Common.h"
#include "../libexecutor/ExecutiveContext.h"
#include <bcos-framework/libutilities/Common.h>
#include <bcos-framework/interfaces/storage/TableInterface.h>
#include <bcos-framework/libcodec/abi/ContractABICodec.h>

namespace bcos {
namespace precompiled {
    static const std::string USER_TABLE_PREFIX_SHORT = "u_";
    static const std::string CONTRACT_TABLE_PREFIX_SHORT = "c_";
    inline void getErrorCodeOut(bytes &out, int const &result){
        bcos::codec::abi::ContractABICodec abi(nullptr);
        if (result >= 0 && result < 128)
        {
            out = abi.abiIn("", u256(result));
            return;
        }
        out = abi.abiIn("", s256(result));
    }
    inline std::string getTableName(const std::string &_tableName) {
        return USER_TABLE_PREFIX_SHORT + _tableName;
    }
    inline std::string getContractTableName(const std::string &_contractAddress) {

        return CONTRACT_TABLE_PREFIX_SHORT + _contractAddress;
    }

    void checkNameValidate(const std::string &tableName, std::string &keyField,
                           std::vector<std::string> &valueFieldList);
    int checkLengthValidate(const std::string &field_value, int32_t max_length,
                            int32_t errorCode);

    uint32_t getFuncSelector(std::string const& _functionName);
    uint32_t getParamFunc(bytesConstRef _param);
    uint32_t getFuncSelectorByFunctionName(std::string const &_functionName);

    bcos::precompiled::ContractStatus
    getContractStatus(std::shared_ptr<bcos::executor::ExecutiveContext> _context,
                      std::string const& _tableName);

    bytesConstRef getParamData(bytesConstRef _param);

} // namespace precompiled
} // namespace bcos