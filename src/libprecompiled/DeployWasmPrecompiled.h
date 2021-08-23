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
 * @file deployWasmPrecompiled.h
 * @author: kyonRay
 * @date 2021-06-17
 */

#pragma once
#include "../libvm/BlockContext.h"
#include "../libvm/Precompiled.h"
#include "Common.h"
#include <bcos-framework/interfaces/storage/Common.h>
#include <bcos-framework/interfaces/storage/TableInterface.h>

namespace bcos
{
namespace precompiled
{
#if 0
deployWASM(bytes code, bytes param, string path, string jsonABI) => bool;
#endif
class DeployWasmPrecompiled : public bcos::precompiled::Precompiled
{
public:
    using Ptr = std::shared_ptr<DeployWasmPrecompiled>;
    DeployWasmPrecompiled(crypto::Hash::Ptr _hashImpl);
    virtual ~DeployWasmPrecompiled() = default;
    std::shared_ptr<PrecompiledExecResult> call(std::shared_ptr<executor::BlockContext> _context,
        bytesConstRef _param, const std::string& _origin, const std::string& _sender,
        u256& _remainGas) override;
    std::string toString() override;

private:
    bool setContractFile(std::shared_ptr<executor::BlockContext> _tableFactory,
        const std::string& _parentDir, const std::string& _contractName);
    bool checkPathValid(std::string const& _path);
};
}  // namespace precompiled
}  // namespace bcos
