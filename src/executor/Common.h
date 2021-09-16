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
 * @brief executor Common
 * @file Common.h
 * @author: xingqiangbai
 * @date: 2021-05-26
 */

#pragma once

#include "bcos-framework/libutilities/FixedBytes.h"
#include "bcos-framework/libutilities/Log.h"
#include <boost/format.hpp>
#include <memory>

#define EXECUTOR_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("EXECUTOR")
#define PARA_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("PARA") << LOG_BADGE(utcTime())

namespace bcos
{
namespace executor
{
    // template <class T>
    // inline std::string fromHex(const T& in) {
    //     // return boost::format();
    // }
}  // namespace executor
}  // namespace bcos
