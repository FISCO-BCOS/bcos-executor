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
 * @file Precompiled.h
 * @author: kyonRay
 * @date 2021-05-25
 */

#pragma once
#include "PrecompiledResult.h"
#include "../libexecutor/ExecutiveContext.h"
#include <bcos-framework/interfaces/storage/TableInterface.h>

namespace bcos
{
namespace precompiled
{
class Precompiled : public std::enable_shared_from_this<Precompiled>
{
public:
  using Ptr = std::shared_ptr<Precompiled>;

  virtual ~Precompiled() = default;
  virtual std::string toString() { return ""; }
  virtual PrecompiledExecResult::Ptr
  call(std::shared_ptr<executor::ExecutiveContext> _context,
       bytesConstRef _param, const std::string &_origin,
       const std::string &_sender, u256& _remainGas) = 0;

  virtual bool isParallelPrecompiled() { return false; }
  virtual std::vector<std::string> getParallelTag(bytesConstRef /*param*/)
  {
    return std::vector<std::string>();
  }

protected:
  std::map<std::string, uint32_t> name2Selector;

protected:
    bcos::storage::TableInterface::Ptr
    createTable(storage::TableFactoryInterface::Ptr _tableFactory,
                const std::string &_tableName, const std::string &_keyField,
                const std::string &_valueField);

    bool checkAuthority(storage::TableFactoryInterface::Ptr _tableFactory,
                      const std::string &_origin, const std::string &_contract);

  PrecompiledGasFactory::Ptr m_precompiledGasFactory;
};

} // namespace precompiled
} // namespace bcos
