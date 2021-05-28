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
 * @file Precompiled.cpp
 * @author: kyonRay
 * @date 2021-05-25
 */

#include "Precompiled.h"
#include <tbb/concurrent_unordered_map.h>
#include "Utilities.h"

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;

storage::TableInterface::Ptr Precompiled::createTable(
        storage::TableFactoryInterface::Ptr _tableFactory, const std::string& tableName,
    const std::string& keyField, const std::string& valueField)
{
    auto ret = _tableFactory->createTable(tableName, keyField, valueField);
    if (!ret) {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("createTable") << LOG_DESC("") << LOG_KV("tableName", tableName);
        return nullptr;
    } else {
        return _tableFactory->openTable(tableName);
    }
}

bool Precompiled::checkAuthority(
        storage::TableFactoryInterface::Ptr _tableFactory,
    const std::string& _origin, const std::string& _contract)
{
  auto tableName = getContractTableName(_contract);
  return _tableFactory->checkAuthority(tableName, _origin);
}