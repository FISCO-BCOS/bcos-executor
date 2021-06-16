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
 * @file EntryPrecompiled.h
 * @author: kyonRay
 * @date 2021-05-26
 */

#pragma once

#include "../libvm/Precompiled.h"
#include <bcos-framework/interfaces/storage/Common.h>
#include "../libvm/ExecutiveContext.h"

namespace bcos
{
namespace precompiled
{
#if 0
contract Entry {
    function getInt(string) public constant returns(int);
    function getBytes32(string) public constant returns(bytes32);
    function getBytes64(string) public constant returns(byte[64]);
    function getAddress(string) public constant returns(address);

    function set(string, int) public;
    function set(string, string) public;
}
{
    "fda69fae": "getInt(string)",
    "d52decd4": "getBytes64(string)",
    "27314f79": "getBytes32(string)",
    "bf40fac1": "getAddress(string)"
    "2ef8ba74": "set(string,int256)",
    "e942b516": "set(string,string)",
}
#endif

class EntryPrecompiled : public bcos::precompiled::Precompiled
{
public:
    using Ptr = std::shared_ptr<EntryPrecompiled>;
    EntryPrecompiled(crypto::Hash::Ptr _hashImpl);
    virtual ~EntryPrecompiled(){};

    std::string toString() override;

    std::shared_ptr<PrecompiledExecResult> call(std::shared_ptr<executor::ExecutiveContext> _context,
        bytesConstRef _param, const std::string& _origin, const std::string& _sender,
        u256& _remainGas) override;

    void setEntry(bcos::storage::Entry::Ptr entry) { m_entry = entry; }
    bcos::storage::Entry::Ptr getEntry() const { return m_entry; };

private:
    bcos::storage::Entry::Ptr m_entry;
};
}  // namespace precompiled
}  // namespace dev