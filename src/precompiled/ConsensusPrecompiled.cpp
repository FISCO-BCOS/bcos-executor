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
#include <boost/archive/basic_archive.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/core/ignore_unused.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/serialization/vector.hpp>
#include <tuple>
#include <utility>

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

std::shared_ptr<PrecompiledExecResult> ConsensusPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _param,
    const std::string&, const std::string&)
{
    // parse function name
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);

    auto callResult = std::make_shared<PrecompiledExecResult>();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();

    showConsensusTable(_executive);

    auto blockContext = _executive->blockContext().lock();
    auto codec = PrecompiledCodec(blockContext->hashHandler(), blockContext->isWasm());

    int result = 0;
    if (func == name2Selector[CSS_METHOD_ADD_SEALER])
    {
        // addSealer(string, uint256)
        result = addSealer(_executive, data, codec);
    }
    else if (func == name2Selector[CSS_METHOD_ADD_SER])
    {
        // addObserver(string)
        result = addObserver(_executive, data, codec);
    }
    else if (func == name2Selector[CSS_METHOD_REMOVE])
    {
        // remove(string)
        result = removeNode(_executive, data, codec);
    }
    else if (func == name2Selector[CSS_METHOD_SET_WEIGHT])
    {
        // setWeight(string,uint256)
        result = setWeight(_executive, data, codec);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ConsensusPrecompiled")
                               << LOG_DESC("call undefined function") << LOG_KV("func", func);
    }

    getErrorCodeOut(callResult->mutableExecResult(), result, codec);
    gasPricer->updateMemUsed(callResult->m_execResult.size());
    callResult->setGas(gasPricer->calTotalGas());
    return callResult;
}

int ConsensusPrecompiled::addSealer(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& _data,
    const PrecompiledCodec& codec)
{
    // addSealer(string, uint256)
    std::string nodeID;
    u256 weight;
    auto blockContext = _executive->blockContext().lock();
    codec.decode(_data, nodeID, weight);
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
    if (weight == 0)
    {
        // u256 weight be then 0
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("weight is 0")
                               << LOG_KV("nodeID", nodeID);
        return CODE_INVALID_WEIGHT;
    }

    auto& storage = _executive->storage();

    ConsensusNodeList consensusList;
    auto entry = storage.getRow(SYS_CONSENSUS, "key");
    if (entry)
    {
        auto value = entry->getField(0);
        consensusList = decodeConsensusList(value);
    }
    else
    {
        entry.emplace(Entry());
    }

    auto it = std::find_if(consensusList.begin(), consensusList.end(),
        [&nodeID](const ConsensusNode& node) { return node.nodeID == nodeID; });
    if (it != consensusList.end())
    {
        it->weight = weight;
        it->type = ledger::CONSENSUS_SEALER;
        it->enableNumber = boost::lexical_cast<std::string>(blockContext->number() + 1);
    }
    else
    {
        consensusList.emplace_back(nodeID, weight, ledger::CONSENSUS_SEALER,
            boost::lexical_cast<std::string>(blockContext->number() + 1));
    }

    auto encodedValue = encodeConsensusList(consensusList);
    entry->importFields({std::move(encodedValue)});

    storage.setRow(SYS_CONSENSUS, "key", std::move(*entry));

    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled")
                           << LOG_DESC("addSealer successfully insert") << LOG_KV("nodeID", nodeID)
                           << LOG_KV("weight", weight);
    return 0;
}

int ConsensusPrecompiled::addObserver(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& _data,
    const PrecompiledCodec& codec)
{
    // addObserver(string)
    std::string nodeID;
    auto blockContext = _executive->blockContext().lock();
    codec.decode(_data, nodeID);
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

    auto& storage = _executive->storage();

    auto entry = storage.getRow(SYS_CONSENSUS, "key");

    ConsensusNodeList consensusList;
    if (entry)
    {
        auto value = entry->getField(0);
        consensusList = decodeConsensusList(value);
    }
    else
    {
        entry.emplace(Entry());
    }
    auto it = std::find_if(consensusList.begin(), consensusList.end(),
        [&nodeID](const ConsensusNode& node) { return node.nodeID == nodeID; });
    if (it != consensusList.end())
    {
        it->weight = 0;
        it->type = ledger::CONSENSUS_OBSERVER;
        it->enableNumber = boost::lexical_cast<std::string>(blockContext->number() + 1);
    }
    else
    {
        consensusList.emplace_back(nodeID, 0, ledger::CONSENSUS_OBSERVER,
            boost::lexical_cast<std::string>(blockContext->number() + 1));
    }

    auto sealerCount = std::count_if(consensusList.begin(), consensusList.end(),
        [](auto&& node) { return node.type == ledger::CONSENSUS_SEALER; });

    if (sealerCount == 0)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled")
                               << LOG_DESC("addObserver failed, because last sealer");
        return CODE_LAST_SEALER;
    }

    auto encodedValue = encodeConsensusList(consensusList);
    entry->importFields({std::move(encodedValue)});

    storage.setRow(SYS_CONSENSUS, "key", std::move(*entry));

    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled")
                           << LOG_DESC("addObserver successfully insert");
    return 0;
}

int ConsensusPrecompiled::removeNode(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& _data,
    const PrecompiledCodec& codec)
{
    // remove(string)
    std::string nodeID;
    auto blockContext = _executive->blockContext().lock();
    codec.decode(_data, nodeID);
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

    auto& storage = _executive->storage();

    ConsensusNodeList consensusList;
    auto entry = storage.getRow(SYS_CONSENSUS, "key");
    if (entry)
    {
        auto value = entry->getField(0);

        consensusList = decodeConsensusList(value);
    }
    else
    {
        entry.emplace(Entry());
    }
    auto it = std::find_if(consensusList.begin(), consensusList.end(),
        [&nodeID](const ConsensusNode& node) { return node.nodeID == nodeID; });
    if (it != consensusList.end())
    {
        it = consensusList.erase(it);
    }

    auto sealerSize = std::count_if(consensusList.begin(), consensusList.end(),
        [](auto&& node) { return node.type == ledger::CONSENSUS_SEALER; });

    if (sealerSize == 0)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled")
                               << LOG_DESC("addObserver failed, because last sealer");
        return CODE_LAST_SEALER;
    }

    auto encodedValue = encodeConsensusList(consensusList);
    entry->importFields({std::move(encodedValue)});

    storage.setRow(SYS_CONSENSUS, "key", std::move(*entry));

    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("remove successfully");
    return 0;
}

int ConsensusPrecompiled::setWeight(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& _data,
    const PrecompiledCodec& codec)
{
    // setWeight(string,uint256)
    std::string nodeID;
    u256 weight;
    auto blockContext = _executive->blockContext().lock();
    codec.decode(_data, nodeID, weight);
    // Uniform lowercase nodeID
    boost::to_lower(nodeID);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("setWeight func")
                           << LOG_KV("nodeID", nodeID);
    if (nodeID.size() != 128u)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ConsensusPrecompiled")
                               << LOG_DESC("nodeID length error") << LOG_KV("nodeID", nodeID);
        return CODE_INVALID_NODE_ID;
    }
    if (weight == 0)
    {
        // u256 weight must greater then 0
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("weight is 0")
                               << LOG_KV("nodeID", nodeID);
        return CODE_INVALID_WEIGHT;
    }

    auto& storage = _executive->storage();

    auto entry = storage.getRow(SYS_CONSENSUS, "key");

    ConsensusNodeList consensusList;
    if (entry)
    {
        auto value = entry->getField(0);

        consensusList = decodeConsensusList(value);
    }
    auto it = std::find_if(consensusList.begin(), consensusList.end(),
        [&nodeID](const ConsensusNode& node) { return node.nodeID == nodeID; });
    if (it != consensusList.end())
    {
        it->weight = 0;
        it->enableNumber = boost::lexical_cast<std::string>(blockContext->number() + 1);
    }
    else
    {
        return CODE_NODE_NOT_EXIST;  // Not found
    }

    auto encodedValue = encodeConsensusList(consensusList);
    entry->importFields({std::move(encodedValue)});

    storage.setRow(SYS_CONSENSUS, "key", std::move(*entry));

    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled")
                           << LOG_DESC("setWeight successfully");
    return 0;
}

void ConsensusPrecompiled::showConsensusTable(
    const std::shared_ptr<executor::TransactionExecutive>& _executive)
{
    auto& storage = _executive->storage();
    if (!storage.openTable(SYS_CONSENSUS))
    {
        storage.createTable(SYS_CONSENSUS, "type,weight,enable_number");
    }

    auto entry = storage.getRow(SYS_CONSENSUS, "key");

    if (!entry)
    {
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("ConsensusPrecompiled")
                               << LOG_DESC("showConsensusTable") << " No consensus";
        return;
    }

    auto value = entry->getField(0);

    auto consensusList = decodeConsensusList(value);

    std::stringstream s;
    s << "ConsensusPrecompiled show table:\n";
    for (auto& it : consensusList)
    {
        auto& [nodeID, weight, type, enableNumber] = it;

        s << "ConsensusPrecompiled: " << nodeID << "," << type << "," << enableNumber << ","
          << weight << "\n";
    }
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("showConsensusTable")
                           << LOG_KV("consensusTable", s.str());
}