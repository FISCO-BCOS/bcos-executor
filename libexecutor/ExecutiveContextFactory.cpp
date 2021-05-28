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
 * @file ExecutiveContextFactory.cpp
 * @author: xingqiangbai
 * @date: 2021-05-27
 */
#include "ExecutiveContextFactory.h"
#include "../libprecompiled/Utilities.h"
#include "../libstate/State.h"
#include "Common.h"
#include "bcos-framework/libtable/TableFactory.h"
// #include "include/UserPrecompiled.h"
#include "../libprecompiled/CNSPrecompiled.h"
// #include "../libprecompiled/CRUDPrecompiled.h"
// #include "../libprecompiled/ChainGovernancePrecompiled.h"
#include "../libprecompiled/ConsensusPrecompiled.h"
// #include "../libprecompiled/ContractLifeCyclePrecompiled.h"
#include "../libprecompiled/KVTableFactoryPrecompiled.h"
#include "../libprecompiled/ParallelConfigPrecompiled.h"
// #include "../libprecompiled/PermissionPrecompiled.h"
#include "../libprecompiled/CryptoPrecompiled.h"
#include "../libprecompiled/PrecompiledResult.h"
#include "../libprecompiled/SystemConfigPrecompiled.h"
#include "../libprecompiled/TableFactoryPrecompiled.h"
// #include "../libprecompiled/WorkingSealerManagerPrecompiled.h"
#include "../libprecompiled/extension/DagTransferPrecompiled.h"
#include "../libvm/ExecutiveContext.h"
#include "bcos-framework/libcodec/abi/ContractABIType.h"

using namespace std;
using namespace bcos;
using namespace bcos::storage;
using namespace bcos::executor;
using namespace bcos::precompiled;

ExecutiveContext::Ptr ExecutiveContextFactory::createExecutiveContext(BlockInfo blockInfo)
{
    auto tableFactory =
        std::make_shared<TableFactory>(m_stateStorage, m_hashImpl, blockInfo.number);
    ExecutiveContext::Ptr context = make_shared<ExecutiveContext>(tableFactory, m_hashImpl);
    auto tableFactoryPrecompiled = std::make_shared<precompiled::TableFactoryPrecompiled>();
    tableFactoryPrecompiled->setMemoryTableFactory(tableFactory);
    context->setAddress2Precompiled(
        SYS_CONFIG_ADDRESS, std::make_shared<precompiled::SystemConfigPrecompiled>());
    context->setAddress2Precompiled(TABLE_FACTORY_ADDRESS, tableFactoryPrecompiled);
    // context->setAddress2Precompiled(CRUD_ADDRESS,
    // std::make_shared<precompiled::CRUDPrecompiled>());
    context->setAddress2Precompiled(
        CONSENSUS_ADDRESS, std::make_shared<precompiled::ConsensusPrecompiled>());
    context->setAddress2Precompiled(CNS_ADDRESS, std::make_shared<precompiled::CNSPrecompiled>());
    // context->setAddress2Precompiled(
    //     PERMISSION_ADDRESS, std::make_shared<precompiled::PermissionPrecompiled>());

    auto parallelConfigPrecompiled = std::make_shared<precompiled::ParallelConfigPrecompiled>();
    context->setAddress2Precompiled(PARALLEL_CONFIG_ADDRESS, parallelConfigPrecompiled);
    // context->setAddress2Precompiled(
    // CONTRACT_LIFECYCLE_ADDRESS, std::make_shared<precompiled::ContractLifeCyclePrecompiled>());
    auto kvTableFactoryPrecompiled = std::make_shared<precompiled::KVTableFactoryPrecompiled>();
    kvTableFactoryPrecompiled->setMemoryTableFactory(tableFactory);
    context->setAddress2Precompiled(KV_TABLE_FACTORY_ADDRESS, kvTableFactoryPrecompiled);
    // context->setAddress2Precompiled(
    //     CHAINGOVERNANCE_ADDRESS, std::make_shared<precompiled::ChainGovernancePrecompiled>());

    // register User developed Precompiled contract
    registerUserPrecompiled(context);
    context->setMemoryTableFactory(tableFactory);
    context->setBlockInfo(blockInfo);
    context->setPrecompiledContract(m_precompiledContract);
    auto state = make_shared<State>(tableFactory, m_hashImpl);
    context->setState(state);

    setTxGasLimitToContext(context);

    // register workingSealerManagerPrecompiled for VRF-based-rPBFT

    // context->setAddress2Precompiled(WORKING_SEALER_MGR_ADDRESS,
    //     std::make_shared<precompiled::WorkingSealerManagerPrecompiled>());
    context->setAddress2Precompiled(CRYPTO_ADDRESS, std::make_shared<CryptoPrecompiled>());
    context->setTxCriticalsHandler([&](const protocol::Transaction::ConstPtr& _tx)
                                       -> std::shared_ptr<std::vector<std::string>> {
        if (_tx->type() == protocol::TransactionType::ContractCreation)
        {
            // Not to parallel contract creation transaction
            return nullptr;
        }

        auto p = context->getPrecompiled(_tx->to().toString());
        if (p)
        {
            // Precompile transaction
            if (p->isParallelPrecompiled())
            {
                auto ret = make_shared<vector<string>>(p->getParallelTag(_tx->input()));
                for (string& critical : *ret)
                {
                    critical += _tx->to().toString();
                }
                return ret;
            }
            else
            {
                return nullptr;
            }
        }
        else
        {
            uint32_t selector = precompiled::getParamFunc(_tx->input());

            auto receiveAddress = _tx->to().toString();
            std::shared_ptr<precompiled::ParallelConfig> config = nullptr;
            // hit the cache, fetch ParallelConfig from the cache directly
            // Note: Only when initializing DAG, get ParallelConfig, will not get ParallelConfig
            // during transaction execution
            auto parallelKey = std::make_pair(receiveAddress, selector);
            config = parallelConfigPrecompiled->getParallelConfig(
                context, receiveAddress, selector, _tx->sender().toString());

            if (config == nullptr)
            {
                return nullptr;
            }
            else
            {
                // Testing code
                auto res = make_shared<vector<string>>();

                codec::abi::ABIFunc af;
                bool isOk = af.parser(config->functionName);
                if (!isOk)
                {
                    EXECUTOR_LOG(DEBUG)
                        << LOG_DESC("[getTxCriticals] parser function signature failed, ")
                        << LOG_KV("func signature", config->functionName);

                    return nullptr;
                }

                auto paramTypes = af.getParamsType();
                if (paramTypes.size() < (size_t)config->criticalSize)
                {
                    EXECUTOR_LOG(DEBUG)
                        << LOG_DESC("[getTxCriticals] params type less than  criticalSize")
                        << LOG_KV("func signature", config->functionName)
                        << LOG_KV("func criticalSize", config->criticalSize);

                    return nullptr;
                }

                paramTypes.resize((size_t)config->criticalSize);

                codec::abi::ContractABICodec abi(m_hashImpl);
                isOk = abi.abiOutByFuncSelector(_tx->input().getCroppedData(4), paramTypes, *res);
                if (!isOk)
                {
                    EXECUTOR_LOG(DEBUG) << LOG_DESC("[getTxCriticals] abiout failed, ")
                                        << LOG_KV("func signature", config->functionName);

                    return nullptr;
                }

                for (string& critical : *res)
                {
                    critical += _tx->to().toString();
                }

                return res;
            }
        }
    });
    return context;
}

void ExecutiveContextFactory::setStateStorage(StorageInterface::Ptr stateStorage)
{
    m_stateStorage = stateStorage;
}

void ExecutiveContextFactory::setTxGasLimitToContext(ExecutiveContext::Ptr context)
{
    try
    {  // get value from db
        std::string key = "tx_gas_limit";
        // BlockInfo blockInfo = context->blockInfo();
        // FIXME: get system config from precompiled
        // auto configRet = getSysteConfigByKey(m_stateStorage, key, blockInfo.number);
        // auto ret = configRet->first;
        auto ret = string("FIXME:");
        if (ret != "")
        {
            context->setTxGasLimit(boost::lexical_cast<uint64_t>(ret));
            EXECUTOR_LOG(TRACE) << LOG_DESC("[setTxGasLimitToContext]")
                                << LOG_KV("txGasLimit", context->txGasLimit());
        }
        else
        {
            EXECUTOR_LOG(WARNING) << LOG_DESC("[setTxGasLimitToContext]Tx gas limit is null");
        }
    }
    catch (std::exception& e)
    {
        EXECUTOR_LOG(ERROR) << LOG_DESC("[setTxGasLimitToContext]Failed")
                            << LOG_KV("EINFO", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(e);
    }
}
