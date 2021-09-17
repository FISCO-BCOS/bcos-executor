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
 * @file PermissionPrecompiled.h
 * @author: kyonRay
 * @date 2021-09-03
 */

#pragma once
#include "PermissionPrecompiledInterface.h"
#include "../Common.h"

namespace bcos::precompiled
{
class PermissionPrecompiled : public PermissionPrecompiledInterface
{
public:
    typedef std::shared_ptr<PermissionPrecompiled> Ptr;
    PermissionPrecompiled(crypto::Hash::Ptr _hashImpl);
    virtual ~PermissionPrecompiled(){};

    std::string toString() override;

    std::shared_ptr<PrecompiledExecResult> call(std::shared_ptr<executor::BlockContext> _context,
        bytesConstRef _param, const std::string& _origin, const std::string& _sender,
        int64_t _remainGas) override;

    PermissionRet::Ptr login(const std::string& nonce, const std::vector<std::string>& params) override;

    PermissionRet::Ptr logout(const std::string& path, const std::vector<std::string>& params) override;
    PermissionRet::Ptr create(const std::string& userPath, const std::string& origin,
        const std::string& to, bytesConstRef params) override;

    PermissionRet::Ptr call(const std::string& userPath, const std::string& origin,
        const std::string& to, bytesConstRef params) override;

    PermissionRet::Ptr sendTransaction(const std::string& userPath, const std::string& origin,
        const std::string& to, bytesConstRef params) override;
};
}  // namespace bcos::precompiled
