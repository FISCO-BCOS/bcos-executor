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
 * @file RingSigPrecompiled.h
 * @author: kyonRay
 * @date 2021-05-30
 */

#pragma once
#include "../../libvm/Precompiled.h"
#include "../Common.h"

namespace bcos
{
namespace precompiled
{
#if 0
contract RingSig
{
    function ringSigVerify(string signature, string message, string paramInfo) public constant returns(bool);
}
#endif

class RingSigPrecompiled : public bcos::precompiled::Precompiled
{
public:
    using Ptr = std::shared_ptr<RingSigPrecompiled>;
    RingSigPrecompiled(crypto::Hash::Ptr _hashImpl);
    virtual ~RingSigPrecompiled(){};

    std::shared_ptr<PrecompiledExecResult> call(std::shared_ptr<executor::BlockContext> _context,
        bytesConstRef _param, const std::string& _origin, const std::string& _sender,
        u256& _remainGas) override;
};

}  // namespace precompiled
}  // namespace bcos
