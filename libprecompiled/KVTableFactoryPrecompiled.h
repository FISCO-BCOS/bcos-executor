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
 * @file KVTableFactoryPrecompiled.h
 * @author: kyonRay
 * @date 2021-05-27
 */

#pragma once

#include "../libvm/Precompiled.h"
#include <bcos-framework/interfaces/crypto/CommonType.h>
#include <bcos-framework/interfaces/storage/TableInterface.h>

namespace bcos
{
namespace precompiled
{
#if 0
contract KVTableFactory {
    function openTable(string) public constant returns (KVTable);
    function createTable(string, string, string) public returns (bool,int);
}
#endif

class KVTableFactoryPrecompiled : public bcos::precompiled::Precompiled
{
public:
    using Ptr = std::shared_ptr<KVTableFactoryPrecompiled>;
    KVTableFactoryPrecompiled(crypto::Hash::Ptr _hashImpl);
    virtual ~KVTableFactoryPrecompiled(){};

    std::string toString() override;

    std::shared_ptr<PrecompiledExecResult> call(std::shared_ptr<executor::BlockContext> _context,
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
    void checkCreateTableParam(
        std::string _tableName, std::string _keyFiled, std::string _valueField);
    bcos::storage::TableFactoryInterface::Ptr m_memoryTableFactory;
};

}  // namespace precompiled
}  // namespace bcos