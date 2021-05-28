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
 * @brief evm precompiled contract
 * @file PrecompiledContract.cpp
 * @date: 2021-05-24
 */

#include "PrecompiledContract.h"
using namespace bcos;
using namespace bcos::executor;

PrecompiledContract::PrecompiledContract(
    unsigned _base, unsigned _word, PrecompiledExecutor const& _exec, u256 const& _startingBlock)
  : PrecompiledContract(
        [=](bytesConstRef _in) -> bigint {
            bigint s = _in.size();
            bigint b = _base;
            bigint w = _word;
            return b + (s + 31) / 32 * w;
        },
        _exec, _startingBlock)
{}
