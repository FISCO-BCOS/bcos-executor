/*
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
 * @brief factory of ExecutiveContext
 * @file ExecutiveContextFactory.h
 * @author: xingqiangbai
 * @date: 2021-05-27
 */
#pragma once

#include "../libvm/ExecutiveContext.h"
#include "bcos-framework/interfaces/storage/StorageInterface.h"
#include "bcos-framework/libtable/Table.h"

namespace bcos
{
namespace precompiled
{
class PrecompiledGasFactory;
}  // namespace precompiled

namespace executor
{
class ExecutiveContext;
class ExecutiveContextFactory : public std::enable_shared_from_this<ExecutiveContextFactory>
{
public:
    typedef std::shared_ptr<ExecutiveContextFactory> Ptr;
    ExecutiveContextFactory()
    {
        m_precompiledContract.insert(std::make_pair(std::string("0x1"),
            PrecompiledContract(3000, 0, PrecompiledRegistrar::executor("ecrecover"))));
        m_precompiledContract.insert(std::make_pair(std::string("0x2"),
            PrecompiledContract(60, 12, PrecompiledRegistrar::executor("sha256"))));
        m_precompiledContract.insert(std::make_pair(std::string("0x3"),
            PrecompiledContract(600, 120, PrecompiledRegistrar::executor("ripemd160"))));
        m_precompiledContract.insert(std::make_pair(std::string("0x4"),
            PrecompiledContract(15, 3, PrecompiledRegistrar::executor("identity"))));
        m_precompiledContract.insert(
            {std::string("0x5"), PrecompiledContract(PrecompiledRegistrar::pricer("modexp"),
                                     PrecompiledRegistrar::executor("modexp"))});
        m_precompiledContract.insert({std::string("0x6"),
            PrecompiledContract(150, 0, PrecompiledRegistrar::executor("alt_bn128_G1_add"))});
        m_precompiledContract.insert({std::string("0x7"),
            PrecompiledContract(6000, 0, PrecompiledRegistrar::executor("alt_bn128_G1_mul"))});
        m_precompiledContract.insert({std::string("0x8"),
            PrecompiledContract(PrecompiledRegistrar::pricer("alt_bn128_pairing_product"),
                PrecompiledRegistrar::executor("alt_bn128_pairing_product"))});
        m_precompiledContract.insert({std::string("0x9"),
            PrecompiledContract(PrecompiledRegistrar::pricer("blake2_compression"),
                PrecompiledRegistrar::executor("blake2_compression"))});
    };
    virtual ~ExecutiveContextFactory(){};

    virtual std::shared_ptr<ExecutiveContext> createExecutiveContext(BlockInfo blockInfo);

    virtual void setStateStorage(storage::StorageInterface::Ptr stateStorage);

private:
    crypto::Hash::Ptr m_hashImpl;
    storage::StorageInterface::Ptr m_stateStorage;
    std::unordered_map<std::string, PrecompiledContract> m_precompiledContract;


    void setTxGasLimitToContext(std::shared_ptr<ExecutiveContext> context);
    void registerUserPrecompiled(std::shared_ptr<ExecutiveContext> context);
};

}  // namespace executor

}  // namespace bcos
