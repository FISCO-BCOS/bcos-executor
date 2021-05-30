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
 * @file CRUDPrecompiled.h
 * @author: kyonRay
 * @date 2021-05-31
 */

#pragma once
#include "Common.h"
#include "Precompiled.h"
#include "../libexecutor/ExecutiveContext.h"

namespace bcos
{
namespace precompiled
{
class CRUDPrecompiled : public bcos::precompiled::Precompiled
{
public:
    using Ptr = std::shared_ptr<CRUDPrecompiled>;
    CRUDPrecompiled();
    virtual ~CRUDPrecompiled(){};

    std::string toString() override;

    PrecompiledExecResult::Ptr call(std::shared_ptr<executor::ExecutiveContext> _context,
        bytesConstRef _param, const std::string& _origin, const std::string& _sender,
        u256& _remainGas) override;

private:
    int parseEntry(const std::string& entryStr, storage::Entry::Ptr& entry);
    int parseCondition(const std::string& conditionStr, storage::Condition::Ptr& condition,
        PrecompiledExecResult::Ptr _execResult);
};
}  // namespace precompiled
}  // namespace bcos