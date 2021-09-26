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
 * @file KVTablePrecompiled.h
 * @author: kyonRay
 * @date 2021-05-27
 */

#pragma once

#include "../vm/Precompiled.h"
#include <bcos-framework/interfaces/crypto/CommonType.h>
#include <bcos-framework/interfaces/storage/Table.h>

namespace bcos
{
namespace precompiled
{
#if 0
contract KVTable {
    function get(string) public constant returns(bool, Entry);
    function set(string, Entry) public returns(bool, int);
    function newEntry() public constant returns(Entry);
}
#endif

class KVTablePrecompiled : public bcos::precompiled::Precompiled
{
public:
    using Ptr = std::shared_ptr<KVTablePrecompiled>;
    KVTablePrecompiled(crypto::Hash::Ptr _hashImpl);
    virtual ~KVTablePrecompiled(){};


    std::string toString() override;

    std::shared_ptr<PrecompiledExecResult> call(std::shared_ptr<executor::BlockContext> _context,
        bytesConstRef _param, const std::string& _origin, const std::string& _sender,
        int64_t _remainGas) override;

    std::shared_ptr<storage::Table> getTable() { return m_table; }
    void setTable(std::shared_ptr<storage::Table> _table) { m_table = _table; }
    // FIXME: table hash
private:
    std::shared_ptr<storage::Table> m_table;
};
}  // namespace precompiled
}  // namespace bcos
