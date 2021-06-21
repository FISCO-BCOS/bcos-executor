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
 * @file SystemConfigPrecompiled.cpp
 * @author: kyonRay
 * @date 2021-05-26
 */

#include "SystemConfigPrecompiled.h"
#include "PrecompiledResult.h"
#include "Utilities.h"
#include <bcos-framework/interfaces/ledger/LedgerTypeDef.h>

using namespace bcos;
using namespace bcos::storage;
using namespace bcos::precompiled;
using namespace bcos::executor;

const char* const SYSCONFIG_METHOD_SET_STR = "setValueByKey(string,string)";
const char* const SYSCONFIG_METHOD_GET_STR = "getValueByKey(string)";

SystemConfigPrecompiled::SystemConfigPrecompiled(crypto::Hash::Ptr _hashImpl)
  : Precompiled(_hashImpl)
{
    name2Selector[SYSCONFIG_METHOD_SET_STR] = getFuncSelector(SYSCONFIG_METHOD_SET_STR, _hashImpl);
    name2Selector[SYSCONFIG_METHOD_GET_STR] = getFuncSelector(SYSCONFIG_METHOD_GET_STR, _hashImpl);
}

PrecompiledExecResult::Ptr SystemConfigPrecompiled::call(
    std::shared_ptr<executor::BlockContext> _context, bytesConstRef _param,
    const std::string& _origin, const std::string&, u256& _remainGas)
{
    // parse function name
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);

    m_codec = std::make_shared<PrecompiledCodec>(_context->hashHandler(), _context->isWasm());
    auto callResult = std::make_shared<PrecompiledExecResult>();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();
    if (func == name2Selector[SYSCONFIG_METHOD_SET_STR])
    {
        int result;
        // setValueByKey(string,string)
        std::string configKey, configValue;
        m_codec->decode(data, configKey, configValue);
        // Uniform lowercase configKey
        boost::to_lower(configKey);
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("SystemConfigPrecompiled")
                               << LOG_DESC("setValueByKey func") << LOG_KV("configKey", configKey)
                               << LOG_KV("configValue", configValue);

        if (!checkValueValid(configKey, configValue))
        {
            PRECOMPILED_LOG(DEBUG)
                << LOG_BADGE("SystemConfigPrecompiled") << LOG_DESC("set invalid value")
                << LOG_KV("configKey", configKey) << LOG_KV("configValue", configValue);
            getErrorCodeOut(
                callResult->mutableExecResult(), CODE_INVALID_CONFIGURATION_VALUES, m_codec);
            return callResult;
        }

        auto tableFactory = _context->getTableFactory();
        auto table = tableFactory->openTable(ledger::SYS_CONFIG);

        auto entry = table->newEntry();
        entry->setField(SYS_VALUE, configValue);
        entry->setField(SYS_CONFIG_ENABLE_BLOCK_NUMBER,
            boost::lexical_cast<std::string>(_context->currentNumber()));
        if (tableFactory->checkAuthority(ledger::SYS_CONFIG, _origin))
        {
            table->setRow(configKey, entry);
            PRECOMPILED_LOG(ERROR)
                << LOG_BADGE("SystemConfigPrecompiled") << LOG_DESC("table commit occurs error")
                << LOG_KV("configKey", configKey);
            // FIXME: use unified code to return
            result = 1;
        }
        else
        {
            PRECOMPILED_LOG(DEBUG)
                << LOG_BADGE("SystemConfigPrecompiled") << LOG_DESC("permission denied");
            // FIXME: use unified code to return
            result = -1;
        }
        getErrorCodeOut(callResult->mutableExecResult(), result, m_codec);
    }
    else if (func == name2Selector[SYSCONFIG_METHOD_GET_STR])
    {
        // getValueByKey(string)
        std::string configKey;
        m_codec->decode(data, configKey);
        // Uniform lowercase configKey
        boost::to_lower(configKey);
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("SystemConfigPrecompiled")
                               << LOG_DESC("getValueByKey func") << LOG_KV("configKey", configKey);

        auto valueNumberPair = getSysConfigByKey(configKey, _context->getTableFactory());

        callResult->setExecResult(
            m_codec->encode(valueNumberPair.first, u256(valueNumberPair.second)));
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("SystemConfigPrecompiled")
                               << LOG_DESC("call undefined function") << LOG_KV("func", func);
    }
    gasPricer->updateMemUsed(callResult->m_execResult.size());
    _remainGas -= gasPricer->calTotalGas();
    return callResult;
}

std::string SystemConfigPrecompiled::toString()
{
    return "SystemConfigPrecompiled";
}

bool SystemConfigPrecompiled::checkValueValid(std::string const& key, std::string const& value)
{
    int64_t configuredValue = 0;
    try
    {
        configuredValue = boost::lexical_cast<int64_t>(value);
    }
    catch (std::exception const& e)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("SystemConfigPrecompiled")
                               << LOG_DESC("checkValueValid failed") << LOG_KV("key", key)
                               << LOG_KV("value", value) << LOG_KV("errorInfo", e.what());
        return false;
    }
    if (ledger::SYSTEM_KEY_TX_COUNT_LIMIT == key)
    {
        return (configuredValue >= TX_COUNT_LIMIT_MIN);
    }
    else if (ledger::SYSTEM_KEY_CONSENSUS_TIMEOUT == key)
    {
        return (configuredValue >= SYSTEM_CONSENSUS_TIMEOUT_MIN &&
                configuredValue < SYSTEM_CONSENSUS_TIMEOUT_MAX);
    }
    return false;
}

std::pair<std::string, protocol::BlockNumber> SystemConfigPrecompiled::getSysConfigByKey(
    const std::string& _key, const storage::TableFactoryInterface::Ptr& _tableFactory) const
{
    auto table = _tableFactory->openTable(ledger::SYS_CONFIG);
    auto entry = table->getRow(_key);
    if (entry)
    {
        auto value = entry->getField(SYS_VALUE);
        auto enableNumber = boost::lexical_cast<protocol::BlockNumber>(
            entry->getField(SYS_CONFIG_ENABLE_BLOCK_NUMBER));
        return {value, enableNumber};
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("SystemConfigPrecompiled")
                               << LOG_DESC("get sys config error") << LOG_KV("configKey", _key);
        // FIXME: use unified code to return
        return {"", -1};
    }
}