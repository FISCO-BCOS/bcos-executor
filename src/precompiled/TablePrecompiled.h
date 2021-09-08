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
 * @file TablePrecompiled.h
 * @author: kyonRay
 * @date 2021-05-30
 */

#pragma once

#include "../vm/Precompiled.h"
#include "Common.h"
#include <bcos-framework/interfaces/crypto/CommonType.h>
#include <bcos-framework/interfaces/storage/Table.h>

namespace bcos
{
#if 0
contract Table {
    function select(string, Condition) public constant returns(Entries);
    function insert(string, Entry) public returns(int);
    function update(string, Entry, Condition) public returns(int);
    function remove(string, Condition) public returns(int);
    function newEntry() public constant returns(Entry);
    function newCondition() public constant returns(Condition);
}
{
    "31afac36": "insert(string,address)",
    "7857d7c9": "newCondition()",
    "13db9346": "newEntry()",
    "28bb2117": "remove(string,address)",
    "e8434e39": "select(string,address)",
    "bf2b70a1": "update(string,address,address)"
}
#endif
namespace precompiled
{
class TablePrecompiled : public Precompiled
{
public:
    using Ptr = std::shared_ptr<TablePrecompiled>;
    TablePrecompiled(crypto::Hash::Ptr _hashImpl);
    virtual ~TablePrecompiled(){};


    std::string toString() override;

    std::shared_ptr<PrecompiledExecResult> call(std::shared_ptr<executor::BlockContext> _context,
        bytesConstRef _param, const std::string& _origin, const std::string& _sender,
        u256& _remainGas) override;

    std::shared_ptr<bcos::storage::Table> getTable() { return m_table; }
    void setTable(std::shared_ptr<bcos::storage::Table> _table) { m_table = _table; }

    crypto::HashType hash();

private:
    std::shared_ptr<bcos::storage::Table> m_table;
};
}  // namespace precompiled
}  // namespace bcos