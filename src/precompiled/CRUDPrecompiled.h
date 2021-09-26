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
#include "../executive/BlockContext.h"
#include "../vm/Precompiled.h"
#include "Common.h"
#include "Utilities.h"

namespace bcos
{
namespace precompiled
{
class CRUDPrecompiled : public bcos::precompiled::Precompiled
{
public:
    using Ptr = std::shared_ptr<CRUDPrecompiled>;
    CRUDPrecompiled(crypto::Hash::Ptr _hashImpl);
    virtual ~CRUDPrecompiled(){};

    std::string toString() override;

    std::shared_ptr<PrecompiledExecResult> call(std::shared_ptr<executor::BlockContext> _context,
        bytesConstRef _param, const std::string& _origin, const std::string& _sender,
        int64_t _remainGas) override;

private:
    void desc(std::shared_ptr<executor::BlockContext> _context, bytesConstRef _paramData,
        std::shared_ptr<PrecompiledExecResult> _callResult,
        std::shared_ptr<PrecompiledGas> _gasPricer);
    void update(std::shared_ptr<executor::BlockContext> _context, bytesConstRef _paramData,
        std::shared_ptr<PrecompiledExecResult> _callResult,
        std::shared_ptr<PrecompiledGas> _gasPricer);
    void insert(std::shared_ptr<executor::BlockContext> _context, bytesConstRef _paramData,
        std::shared_ptr<PrecompiledExecResult> _callResult,
        std::shared_ptr<PrecompiledGas> _gasPricer);
    void remove(std::shared_ptr<executor::BlockContext> _context, bytesConstRef _paramData,
        std::shared_ptr<PrecompiledExecResult> _callResult,
        std::shared_ptr<PrecompiledGas> _gasPricer);
    void select(std::shared_ptr<executor::BlockContext> _context, bytesConstRef _paramData,
        std::shared_ptr<PrecompiledExecResult> _callResult,
        std::shared_ptr<PrecompiledGas> _gasPricer);
    int parseEntry(const std::string& entryStr, storage::Entry& entry);
    int parseCondition(const std::string& conditionStr, precompiled::Condition::Ptr& condition,
        std::shared_ptr<PrecompiledGas> _gasPricer);
    inline bool isHashField(const std::string_view& _key)
    {
        if (!_key.empty())
        {
            return ((_key.substr(0, 1) != "_" && _key.substr(_key.size() - 1, 1) != "_"));
        }
        return false;
    }
};
}  // namespace precompiled
}  // namespace bcos