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
 * @file ConditionPrecompiled.h
 * @author: kyonRay
 * @date 2021-05-31
 */

#pragma once

#include "Common.h"
#include "Precompiled.h"
#include "Utilities.h"
#include "../libexecutor//ExecutiveContext.h"

namespace bcos
{
namespace precompiled
{
#if 0
contract Condition {
    function EQ(string, int) public view;
    function EQ(string, string) public view;
    function EQ(string, address) public view;

    function NE(string, int) public view;
    function NE(string, string) public view;

    function GT(string, int) public view;
    function GE(string, int) public view;

    function LT(string, int) public view;
    function LE(string, int) public view;

    function limit(int) public view;
    function limit(int, int) public view;
}
{
    "e44594b9": "EQ(string,int256)",
    "cd30a1d1": "EQ(string,string)",
    "42f8dd31": "GE(string,int256)",
    "08ad6333": "GT(string,int256)",
    "b6f23857": "LE(string,int256)",
    "c31c9b65": "LT(string,int256)",
    "39aef024": "NE(string,int256)",
    "2783acf5": "NE(string,string)",
    "2e0d738a": "limit(int256)",
    "7ec1cc65": "limit(int256,int256)"
}
#endif

class ConditionPrecompiled : public Precompiled
{
public:
    using Ptr = std::shared_ptr<ConditionPrecompiled>;
    ConditionPrecompiled();
    virtual ~ConditionPrecompiled(){};


    std::string toString() override;

    PrecompiledExecResult::Ptr call(std::shared_ptr<executor::ExecutiveContext> _context,
        bytesConstRef _param, const std::string& _origin, const std::string& _sender,
        u256& _remainGas) override;

    void setPrecompiledEngine(std::shared_ptr<executor::ExecutiveContext> engine)
    {
        m_exeEngine = engine;
    }

    void setCondition(precompiled::Condition::Ptr condition) { m_condition = condition; }
    precompiled::Condition::Ptr getCondition() { return m_condition; }

private:
    std::shared_ptr<executor::ExecutiveContext> m_exeEngine;
    // condition must been set
    precompiled::Condition::Ptr m_condition;
};
}  // namespace precompiled
}  // namespace bcos
