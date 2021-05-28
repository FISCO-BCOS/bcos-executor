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

#include "../libvm/Precompiled.h"
#include "Common.h"
#include <bcos-framework/interfaces/storage/TableInterface.h>
#include <bcos-framework/interfaces/crypto/CommonType.h>

namespace bcos
{
namespace precompiled
{
#if 0
{
    "56004b6a": "createTable(string,string,string)",
    "f23f63c9": "openTable(string)"
}
contract TableFactory {
    function openTable(string) public constant returns (Table);
    function createTable(string, string, string) public returns (int);
}
#endif

class TableFactoryPrecompiled : public Precompiled
{
public:
    using Ptr = std::shared_ptr<TableFactoryPrecompiled>;
    TableFactoryPrecompiled();
    virtual ~TableFactoryPrecompiled(){};

    std::string toString() override;

    std::shared_ptr<PrecompiledExecResult> call(std::shared_ptr<executor::ExecutiveContext> _context,
        bytesConstRef _param, const std::string& _origin, const std::string& _sender,
        u256& _remainGas) override;

    void setMemoryTableFactory(bcos::storage::TableFactoryInterface::Ptr memoryTableFactory)
    {
        m_memoryTableFactory = memoryTableFactory;
    }

    bcos::storage::TableFactoryInterface::Ptr getMemoryTableFactory()
    {
        return m_memoryTableFactory;
    }

    crypto::HashType hash();

private:
    bcos::storage::TableFactoryInterface::Ptr m_memoryTableFactory;
};
}  // namespace precompiled
}  // namespace bcos
