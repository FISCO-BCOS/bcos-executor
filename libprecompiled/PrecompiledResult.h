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
 * @file PrecompiledResult.h
 * @author: kyonRay
 * @date 2021-05-25
 */
#pragma once
#include "PrecompiledGas.h"
#include <bcos-framework/libutilities/Common.h>

namespace bcos
{
namespace precompiled
{
class PrecompiledExecResult
{
public:
  using Ptr = std::shared_ptr<PrecompiledExecResult>;
  PrecompiledExecResult() = default;
  virtual ~PrecompiledExecResult() {}
  bytes const& execResult() const { return m_execResult; }
  bytes& mutableExecResult() { return m_execResult; }

  void setExecResult(bytes const& _execResult) { m_execResult = _execResult; }

  PrecompiledGas::Ptr gasPricer() { return m_gasPricer; }
  void setGasPricer(PrecompiledGas::Ptr _gasPricer) { m_gasPricer = _gasPricer; }
  virtual u256 calGasCost()
  {
    m_gasPricer->updateMemUsed(m_execResult.size());
    return m_gasPricer->calTotalGas();
  }

private:
  bytes m_execResult;
  PrecompiledGas::Ptr m_gasPricer;
};

class PrecompiledExecResultFactory
{
public:
  using Ptr = std::shared_ptr<PrecompiledExecResultFactory>;

  PrecompiledExecResultFactory() = default;
  virtual ~PrecompiledExecResultFactory() {}
  void setPrecompiledGasFactory(PrecompiledGasFactory::Ptr _precompiledGasFactory)
  {
    m_precompiledGasFactory = _precompiledGasFactory;
  }
  PrecompiledExecResult::Ptr createPrecompiledResult()
  {
    auto result = std::make_shared<PrecompiledExecResult>();
    result->setGasPricer(m_precompiledGasFactory->createPrecompiledGas());
    return result;
  }

private:
  PrecompiledGasFactory::Ptr m_precompiledGasFactory;
};
}  // namespace precompiled
}  // namespace bcos
