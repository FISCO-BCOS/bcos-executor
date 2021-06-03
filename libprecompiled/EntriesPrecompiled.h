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
 * @file EntriesPrecompiled.h
 * @author: kyonRay
 * @date 2021-05-31
 */

#pragma once

#include "../libvm/Precompiled.h"
#include "../libvm/ExecutiveContext.h"
#include "Common.h"

namespace bcos
{
namespace precompiled
{
#if 0
contract Entries {
    function get(int) public constant returns(Entry);
    function size() public constant returns(int);
}
{
    "846719e0": "get(int256)",
    "949d225d": "size()"
}
#endif

class EntriesPrecompiled : public Precompiled
{
public:
    using Ptr = std::shared_ptr<EntriesPrecompiled>;
    EntriesPrecompiled();
    virtual ~EntriesPrecompiled(){};

    std::string toString() override;

    std::shared_ptr<PrecompiledExecResult> call(std::shared_ptr<executor::ExecutiveContext> _context,
        bytesConstRef _param, const std::string& _origin, const std::string& _sender,
        u256& _remainGas) override;

    void setEntries(const EntriesPtr& entries) { m_entriesConst = entries; }
    EntriesPtr getEntriesPtr() { return std::const_pointer_cast<Entries>(m_entriesConst); }
    EntriesConstPtr getEntriesConstPtr() { return m_entriesConst; }

private:
    EntriesConstPtr m_entriesConst;
};
}  // namespace precompiled
}  // namespace bcos