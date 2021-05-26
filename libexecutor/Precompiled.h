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
 * @brief evm precompiled
 * @file Precompiled.h
 * @date: 2021-05-24
 */

#pragma once
#include "bcos-framework/libutilities/Exceptions.h"
#include <functional>
#include <unordered_map>

namespace bcos
{
namespace executor
{
using PrecompiledExecutor = std::function<std::pair<bool, bytes>(bytesConstRef _in)>;
using PrecompiledPricer = std::function<bigint(bytesConstRef _in)>;

DERIVE_BCOS_EXCEPTION(ExecutorNotFound);
DERIVE_BCOS_EXCEPTION(PricerNotFound);

class PrecompiledRegistrar
{
public:
    /// Get the executor object for @a _name function or @throw ExecutorNotFound if not found.
    static PrecompiledExecutor const& executor(std::string const& _name);

    /// Get the price calculator object for @a _name function or @throw PricerNotFound if not found.
    static PrecompiledPricer const& pricer(std::string const& _name);

    /// Register an executor. In general just use ETH_REGISTER_PRECOMPILED.
    static PrecompiledExecutor registerExecutor(
        std::string const& _name, PrecompiledExecutor const& _exec)
    {
        return (get()->m_execs[_name] = _exec);
    }
    /// Unregister an executor. Shouldn't generally be necessary.
    static void unregisterExecutor(std::string const& _name) { get()->m_execs.erase(_name); }

    /// Register a pricer. In general just use ETH_REGISTER_PRECOMPILED_PRICER.
    static PrecompiledPricer registerPricer(
        std::string const& _name, PrecompiledPricer const& _exec)
    {
        return (get()->m_pricers[_name] = _exec);
    }
    /// Unregister a pricer. Shouldn't generally be necessary.
    static void unregisterPricer(std::string const& _name) { get()->m_pricers.erase(_name); }

private:
    static PrecompiledRegistrar* get()
    {
        if (!s_this)
            s_this = new PrecompiledRegistrar;
        return s_this;
    }

    std::unordered_map<std::string, PrecompiledExecutor> m_execs;
    std::unordered_map<std::string, PrecompiledPricer> m_pricers;
    static PrecompiledRegistrar* s_this;
};

// TODO: unregister on unload with a static object.
#define ETH_REGISTER_PRECOMPILED(Name)                                                        \
    static std::pair<bool, bytes> __eth_registerPrecompiledFunction##Name(bytesConstRef _in); \
    static PrecompiledExecutor __eth_registerPrecompiledFactory##Name =                       \
        ::dev::eth::PrecompiledRegistrar::registerExecutor(                                   \
            #Name, &__eth_registerPrecompiledFunction##Name);                                 \
    static std::pair<bool, bytes> __eth_registerPrecompiledFunction##Name
#define ETH_REGISTER_PRECOMPILED_PRICER(Name)                            \
    static bigint __eth_registerPricerFunction##Name(bytesConstRef _in); \
    static PrecompiledPricer __eth_registerPricerFactory##Name =         \
        ::dev::eth::PrecompiledRegistrar::registerPricer(                \
            #Name, &__eth_registerPricerFunction##Name);                 \
    static bigint __eth_registerPricerFunction##Name
}  // namespace eth
}  // namespace dev
