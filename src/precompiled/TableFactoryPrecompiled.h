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
 * @file TableFactoryPrecompiled.h
 * @author: kyonRay
 * @date 2021-05-30
 */

#pragma once

#include "../executive/TransactionExecutive.h"
#include "../vm/Precompiled.h"
#include "Common.h"
#include <bcos-framework/interfaces/crypto/CommonType.h>
#include <bcos-framework/interfaces/storage/Table.h>

namespace bcos
{
namespace precompiled
{
#if 0
{
    "56004b6a": "createTable(string,string,string)",
    "f23f63c9": "openTable(string)"
}
contract StateStorage {
    function openTable(string) public constant returns (Table);
    function createTable(string, string, string) public returns (int);
}
#endif

class TableFactoryPrecompiled : public Precompiled
{
public:
    using Ptr = std::shared_ptr<TableFactoryPrecompiled>;
    TableFactoryPrecompiled(crypto::Hash::Ptr _hashImpl);
    virtual ~TableFactoryPrecompiled(){};

    std::string toString() override;

    std::shared_ptr<PrecompiledExecResult> call(
        std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _param,
        const std::string& _origin, const std::string& _sender) override;

private:
    void openTable(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
        const PrecompiledGas::Ptr& gasPricer);
    void createTable(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
        const std::string& _origin, const std::string& _sender,
        const PrecompiledGas::Ptr& gasPricer);
    void checkCreateTableParam(
        const std::string& _tableName, std::string& _keyFiled, std::string& _valueField);
};
}  // namespace precompiled
}  // namespace bcos
