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

#include "../libvm/Precompiled.h"
#include <bcos-framework/interfaces/storage/TableInterface.h>
#include <bcos-framework/interfaces/crypto/CommonType.h>

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
    typedef std::shared_ptr<KVTablePrecompiled> Ptr;
    KVTablePrecompiled();
    virtual ~KVTablePrecompiled(){};


    std::string toString() override;

    std::shared_ptr<PrecompiledExecResult> call(std::shared_ptr<executor::ExecutiveContext> _context,
        bytesConstRef _param, const std::string& _origin, const std::string& _sender,
        u256& _remainGas) override;

    storage::TableInterface::Ptr getTable() { return m_table; }
    void setTable(storage::TableInterface::Ptr _table) { m_table = _table; }

    crypto::HashType hash();

private:
    storage::TableInterface::Ptr m_table;
};
}  // namespace precompiled
}  // namespace bcos

