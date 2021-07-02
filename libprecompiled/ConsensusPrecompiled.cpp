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
 * @file ConsensusPrecompiled.cpp
 * @author: kyonRay
 * @date 2021-05-26
 */

#include "ConsensusPrecompiled.h"
#include "PrecompiledResult.h"
#include "Utilities.h"
#include <bcos-framework/interfaces/ledger/LedgerTypeDef.h>
#include <bcos-framework/interfaces/protocol/CommonError.h>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::precompiled;
using namespace bcos::ledger;

const char* const CSS_METHOD_ADD_SEALER = "addSealer(string,uint256)";
const char* const CSS_METHOD_ADD_SER = "addObserver(string)";
const char* const CSS_METHOD_REMOVE = "remove(string)";
const char* const CSS_METHOD_SET_WEIGHT = "setWeight(string,uint256)";

ConsensusPrecompiled::ConsensusPrecompiled(crypto::Hash::Ptr _hashImpl) : Precompiled(_hashImpl)
{
    name2Selector[CSS_METHOD_ADD_SEALER] = getFuncSelector(CSS_METHOD_ADD_SEALER, _hashImpl);
    name2Selector[CSS_METHOD_ADD_SER] = getFuncSelector(CSS_METHOD_ADD_SER, _hashImpl);
    name2Selector[CSS_METHOD_REMOVE] = getFuncSelector(CSS_METHOD_REMOVE, _hashImpl);
    name2Selector[CSS_METHOD_SET_WEIGHT] = getFuncSelector(CSS_METHOD_SET_WEIGHT, _hashImpl);
}

PrecompiledExecResult::Ptr ConsensusPrecompiled::call(
    std::shared_ptr<executor::BlockContext> _context, bytesConstRef _param,
    const std::string& _origin, const std::string&, u256& _remainGas)
{
    // parse function name
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);

    m_codec = std::make_shared<PrecompiledCodec>(_context->hashHandler(), _context->isWasm());
    auto callResult = std::make_shared<PrecompiledExecResult>();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();

    showConsensusTable(_context);

    int result = 0;
    if (func == name2Selector[CSS_METHOD_ADD_SEALER])
    {
        // addSealer(string, uint256)
        result = addSealer(_context, data, _origin);
    }
    else if (func == name2Selector[CSS_METHOD_ADD_SER])
    {
        // addObserver(string)
        result = addObserver(_context, data, _origin);
    }
    else if (func == name2Selector[CSS_METHOD_REMOVE])
    {
        // remove(string)
        result = removeNode(_context, data, _origin);
    }
    else if (func == name2Selector[CSS_METHOD_SET_WEIGHT])
    {
        // setWeight(string,uint256)
        result = setWeight(_context, data, _origin);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ConsensusPrecompiled")
                               << LOG_DESC("call undefined function") << LOG_KV("func", func);
    }
    getErrorCodeOut(callResult->mutableExecResult(), result, m_codec);
    gasPricer->updateMemUsed(callResult->m_execResult.size());
    _remainGas -= gasPricer->calTotalGas();
    return callResult;
}

int ConsensusPrecompiled::addSealer(const std::shared_ptr<executor::BlockContext>& _context,
    bytesConstRef& _data, const std::string& _origin)
{
    // addSealer(string, uint256)
    std::string nodeID;
    u256 weight;
    m_codec->decode(_data, nodeID, weight);
    // Uniform lowercase nodeID
    boost::to_lower(nodeID);

    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("addSealer func")
                           << LOG_KV("nodeID", nodeID);
    if (nodeID.size() != 128u)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ConsensusPrecompiled")
                               << LOG_DESC("nodeID length error") << LOG_KV("nodeID", nodeID);
        return CODE_INVALID_NODE_ID;
    }

    auto table = _context->getTableFactory()->openTable(SYS_CONSENSUS);
    auto newEntry = table->newEntry();
    newEntry->setField(NODE_TYPE, ledger::CONSENSUS_SEALER);
    newEntry->setField(
        NODE_ENABLE_NUMBER, boost::lexical_cast<std::string>(_context->currentNumber()));
    newEntry->setField(NODE_WEIGHT, boost::lexical_cast<std::string>(weight));

    if (_context->getTableFactory()->checkAuthority(ledger::SYS_CONSENSUS, _origin))
    {
        table->setRow(nodeID, newEntry);
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled")
                               << LOG_DESC("addSealer successfully insert");
        return 0;
    }
    else
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled")
                               << LOG_DESC("permission denied");
        return CODE_NO_AUTHORIZED;
    }
}

int ConsensusPrecompiled::addObserver(const std::shared_ptr<executor::BlockContext>& _context,
    bytesConstRef& _data, const std::string& _origin)
{
    // addObserver(string)
    std::string nodeID;
    m_codec->decode(_data, nodeID);
    // Uniform lowercase nodeID
    boost::to_lower(nodeID);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("addObserver func")
                           << LOG_KV("nodeID", nodeID);
    if (nodeID.size() != 128u)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ConsensusPrecompiled")
                               << LOG_DESC("nodeID length error") << LOG_KV("nodeID", nodeID);
        return CODE_INVALID_NODE_ID;
    }

    auto table = _context->getTableFactory()->openTable(SYS_CONSENSUS);
    auto nodeIdList = table->getPrimaryKeys(nullptr);

    auto newEntry = table->newEntry();
    newEntry->setField(NODE_TYPE, ledger::CONSENSUS_OBSERVER);
    newEntry->setField(
        NODE_ENABLE_NUMBER, boost::lexical_cast<std::string>(_context->currentNumber()));
    newEntry->setField(NODE_WEIGHT, "-1");
    if (!_context->getTableFactory()->checkAuthority(ledger::SYS_CONSENSUS, _origin))
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled")
                               << LOG_DESC("permission denied");
        return CODE_NO_AUTHORIZED;
    }
    if (checkIsLastSealer(table, nodeID))
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled")
                               << LOG_DESC("addObserver failed, because last sealer");
        return CODE_LAST_SEALER;
    }
    table->setRow(nodeID, newEntry);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled")
                           << LOG_DESC("addObserver successfully insert");
    return 0;
}

int ConsensusPrecompiled::removeNode(const std::shared_ptr<executor::BlockContext>& _context,
    bytesConstRef& _data, const std::string& _origin)
{
    // remove(string)
    std::string nodeID;
    m_codec->decode(_data, nodeID);
    // Uniform lowercase nodeID
    boost::to_lower(nodeID);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("remove func")
                           << LOG_KV("nodeID", nodeID);
    if (nodeID.size() != 128u)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ConsensusPrecompiled")
                               << LOG_DESC("nodeID length error") << LOG_KV("nodeID", nodeID);
        return CODE_INVALID_NODE_ID;
    }

    auto table = _context->getTableFactory()->openTable(ledger::SYS_CONSENSUS);
    if (!_context->getTableFactory()->checkAuthority(ledger::SYS_CONSENSUS, _origin))
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled")
                               << LOG_DESC("permission denied");
        return CODE_NO_AUTHORIZED;
    }
    if (checkIsLastSealer(table, nodeID))
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled")
                               << LOG_DESC("remove failed, because last sealer");
        return CODE_LAST_SEALER;
    }
    table->remove(nodeID);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("remove successfully");
    return 0;
}

int ConsensusPrecompiled::setWeight(const std::shared_ptr<executor::BlockContext>& _context,
    bytesConstRef& _data, const std::string& _origin)
{
    // setWeight(string,uint256)
    std::string nodeID;
    u256 weight;
    m_codec->decode(_data, nodeID, weight);
    // Uniform lowercase nodeID
    boost::to_lower(nodeID);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("remove func")
                           << LOG_KV("nodeID", nodeID);
    if (nodeID.size() != 128u)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ConsensusPrecompiled")
                               << LOG_DESC("nodeID length error") << LOG_KV("nodeID", nodeID);
        return CODE_INVALID_NODE_ID;
    }
    if (weight == 0)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("weight is 0")
                               << LOG_KV("nodeID", nodeID);
        return CODE_INVALID_WEIGHT;
    }
    auto table = _context->getTableFactory()->openTable(ledger::SYS_CONSENSUS);
    auto entry = table->getRow(nodeID);
    if (!entry)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("nodeID not exists")
                               << LOG_KV("nodeID", nodeID);
        return CODE_NODE_NOT_EXIST;
    }
    if (!_context->getTableFactory()->checkAuthority(ledger::SYS_CONSENSUS, _origin))
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled")
                               << LOG_DESC("permission denied");
        return CODE_NO_AUTHORIZED;
    }
    auto newEntry = table->newEntry();
    entry->setField(NODE_WEIGHT, boost::lexical_cast<std::string>(weight));
    table->setRow(nodeID, entry);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("remove successfully");
    return 0;
}

void ConsensusPrecompiled::showConsensusTable(std::shared_ptr<executor::BlockContext> _context)
{
    auto table = _context->getTableFactory()->openTable(ledger::SYS_CONSENSUS);
    auto nodeIdList = table->getPrimaryKeys(nullptr);

    std::stringstream s;
    s << "ConsensusPrecompiled show table:\n";
    for (size_t i = 0; i < nodeIdList.size(); ++i)
    {
        auto entry = table->getRow(nodeIdList.at(i));
        std::string nodeID = nodeIdList.at(i);
        std::string type = entry->getField(NODE_TYPE);
        std::string enableNumber = entry->getField(NODE_ENABLE_NUMBER);
        std::string weight = entry->getField(NODE_WEIGHT);
        s << "ConsensusPrecompiled[" << i << "]:" << nodeID << "," << type << "," << enableNumber
          << "," << weight << "\n";
    }
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("showConsensusTable")
                           << LOG_KV("consensusTable", s.str());
}

std::shared_ptr<std::map<std::string, storage::Entry::Ptr>> ConsensusPrecompiled::getRowsByNodeType(
    bcos::storage::TableInterface::Ptr _table, std::string const& _nodeType)
{
    auto result = std::make_shared<std::map<std::string, storage::Entry::Ptr>>();
    auto keys = _table->getPrimaryKeys(nullptr);
    for (auto& key : keys)
    {
        auto entry = _table->getRow(key);
        if (entry->getField(NODE_TYPE) == _nodeType)
        {
            result->insert(std::make_pair(key, entry));
        }
    }
    return result;
}

bool ConsensusPrecompiled::checkIsLastSealer(
    storage::TableInterface::Ptr _table, std::string const& nodeID)
{
    // Check is last sealer or not.
    auto entryMap = getRowsByNodeType(_table, ledger::CONSENSUS_SEALER);
    if (entryMap->size() == 1u && entryMap->cbegin()->first == nodeID)
    {
        // The nodeID in param is the last sealer, cannot be deleted.
        PRECOMPILED_LOG(WARNING) << LOG_BADGE("ConsensusPrecompiled")
                                 << LOG_DESC(
                                        "ConsensusPrecompiled the nodeID in param is the last "
                                        "sealer, cannot be deleted.")
                                 << LOG_KV("nodeID", nodeID);
        return true;
    }
    return false;
}
