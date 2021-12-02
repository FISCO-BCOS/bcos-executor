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
 * @file CNSPrecompiled.cpp
 * @author: kyonRay
 * @date 2021-05-27
 */

#include "CNSPrecompiled.h"
#include "PrecompiledResult.h"
#include "Utilities.h"
#include <json/json.h>
#include <boost/algorithm/string/trim.hpp>

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::precompiled;
using namespace bcos::protocol;

const char* const CNS_METHOD_INS_STR4 = "insert(string,string,address,string)";
const char* const CNS_METHOD_SLT_STR = "selectByName(string)";
const char* const CNS_METHOD_SLT_STR2 = "selectByNameAndVersion(string,string)";
const char* const CNS_METHOD_GET_CONTRACT_ADDRESS = "getContractAddress(string,string)";

const char* const CNS_METHOD_INS_STR4_WASM = "insert(string,string,string,string)";

CNSPrecompiled::CNSPrecompiled(crypto::Hash::Ptr _hashImpl) : Precompiled(_hashImpl)
{
    name2Selector[CNS_METHOD_INS_STR4] = getFuncSelector(CNS_METHOD_INS_STR4, _hashImpl);
    name2Selector[CNS_METHOD_SLT_STR] = getFuncSelector(CNS_METHOD_SLT_STR, _hashImpl);
    name2Selector[CNS_METHOD_SLT_STR2] = getFuncSelector(CNS_METHOD_SLT_STR2, _hashImpl);
    name2Selector[CNS_METHOD_GET_CONTRACT_ADDRESS] =
        getFuncSelector(CNS_METHOD_GET_CONTRACT_ADDRESS, _hashImpl);

    name2Selector[CNS_METHOD_INS_STR4_WASM] = getFuncSelector(CNS_METHOD_INS_STR4_WASM, _hashImpl);
}
// check param of the cns
int CNSPrecompiled::checkCNSParam(TransactionExecutive::Ptr _executive,
    std::string const& _contractAddress, std::string& _contractName, std::string& _contractVersion,
    std::string const& _contractAbi)
{
    boost::trim(_contractName);
    boost::trim(_contractVersion);
    // check the status of the contract(only print the error message to the log)
    std::string tableName = USER_APPS_PREFIX + _contractAddress;
    ContractStatus contractStatus = getContractStatus(_executive, tableName);

    if (contractStatus != ContractStatus::Available)
    {
        std::stringstream errorMessage;
        errorMessage << "CNS operation failed for ";
        switch (contractStatus)
        {
        case ContractStatus::Frozen:
            errorMessage << "\"" << _contractName
                         << "\" has been frozen, contractAddress = " << _contractAddress;
            break;
        case ContractStatus::AddressNonExistent:
            errorMessage << "the contract \"" << _contractName << "\" with address "
                         << _contractAddress << " does not exist";
            break;
        case ContractStatus::NotContractAddress:
            errorMessage << "invalid address " << _contractAddress
                         << ", please make sure it's a contract address";
            break;
        default:
            errorMessage << "invalid contract \"" << _contractName << "\" with address "
                         << _contractAddress << ", error code:" << std::to_string(contractStatus);
            break;
        }
        PRECOMPILED_LOG(INFO) << LOG_BADGE("CNSPrecompiled") << LOG_DESC(errorMessage.str())
                              << LOG_KV("contractAddress", _contractAddress)
                              << LOG_KV("contractName", _contractName);
    }
    if (_contractVersion.size() > CNS_VERSION_MAX_LENGTH)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("CNSPrecompiled")
                               << LOG_DESC("version length overflow 128")
                               << LOG_KV("contractName", _contractName)
                               << LOG_KV("address", _contractAddress)
                               << LOG_KV("version", _contractVersion);
        return CODE_VERSION_LENGTH_OVERFLOW;
    }
    if (_contractVersion.find(',') != std::string::npos ||
        _contractName.find(',') != std::string::npos)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("CNSPrecompiled")
                               << LOG_DESC("version or name contains \",\"")
                               << LOG_KV("contractName", _contractName)
                               << LOG_KV("version", _contractVersion);
        return CODE_ADDRESS_OR_VERSION_ERROR;
    }
    // check the length of the key
    checkLengthValidate(
        _contractName, CNS_CONTRACT_NAME_MAX_LENGTH, CODE_TABLE_KEY_VALUE_LENGTH_OVERFLOW);
    // check the length of the field value
    checkLengthValidate(
        _contractAbi, USER_TABLE_FIELD_VALUE_MAX_LENGTH, CODE_TABLE_FIELD_VALUE_LENGTH_OVERFLOW);
    return CODE_SUCCESS;
}

std::shared_ptr<PrecompiledExecResult> CNSPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _param,
    const std::string&, const std::string&)
{
    // parse function name
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);

    auto callResult = std::make_shared<PrecompiledExecResult>();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();

    gasPricer->setMemUsed(_param.size());

    if (func == name2Selector[CNS_METHOD_INS_STR4] ||
        func == name2Selector[CNS_METHOD_INS_STR4_WASM])
    {
        // insert(name, version, address, abi), 4 fields in table, the key of table is name field
        insert(_executive, data, callResult, gasPricer);
    }
    else if (func == name2Selector[CNS_METHOD_SLT_STR])
    {
        // selectByName(string) returns(string)
        selectByName(_executive, data, callResult, gasPricer);
    }
    else if (func == name2Selector[CNS_METHOD_SLT_STR2])
    {
        // selectByNameAndVersion(string,string) returns(address,string)
        selectByNameAndVersion(_executive, data, callResult, gasPricer);
    }
    else if (func == name2Selector[CNS_METHOD_GET_CONTRACT_ADDRESS])
    {
        // getContractAddress(string,string) returns(address)
        getContractAddress(_executive, data, callResult, gasPricer);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("CNSPrecompiled") << LOG_DESC("call undefined function")
                               << LOG_KV("func", func);
    }
    gasPricer->updateMemUsed(callResult->m_execResult.size());
    callResult->setGas(gasPricer->calTotalGas());
    return callResult;
}

void CNSPrecompiled::insert(const std::shared_ptr<executor::TransactionExecutive>& _executive,
    bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
    const PrecompiledGas::Ptr& gasPricer)
{
    // insert(name, version, address, abi), 4 fields in table, the key of table is name field
    std::string contractName, contractVersion, contractAbi;
    std::string contractAddress;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    if (blockContext->isWasm())
    {
        codec->decode(data, contractName, contractVersion, contractAddress, contractAbi);
    }
    else
    {
        Address address;
        codec->decode(data, contractName, contractVersion, address, contractAbi);
        contractAddress = address.hex();
    }
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("CNSPrecompiled") << LOG_DESC("insert")
                           << LOG_KV("contractName", contractName)
                           << LOG_KV("contractVersion", contractVersion)
                           << LOG_KV("contractAddress", contractAddress);
    int validCode =
        checkCNSParam(_executive, contractAddress, contractName, contractVersion, contractAbi);
    if (validCode < 0)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("CNSPrecompiled") << LOG_DESC("address invalid")
                               << LOG_KV("address", contractAddress);
        getErrorCodeOut(callResult->mutableExecResult(), CODE_VERSION_LENGTH_OVERFLOW, *codec);
        return;
    }
    auto table = _executive->storage().openTable(SYS_CNS);
    if (!table)
    {
        table = _executive->storage().createTable(SYS_CNS, SYS_VALUE);
    }
    gasPricer->appendOperation(InterfaceOpcode::OpenTable);
    auto entry = table->getRow(contractName);
    if (entry)
    {
        // decode map, check version
        CNSInfoMap cnsInfo;
        auto&& out = asBytes(std::string(entry->getField(SYS_VALUE)));
        codec::scale::decode(cnsInfo, gsl::make_span(out));
        if (cnsInfo.find(contractVersion) != cnsInfo.end())
        {
            PRECOMPILED_LOG(ERROR)
                << LOG_BADGE("CNSPrecompiled") << LOG_DESC("address and version exist")
                << LOG_KV("contractName", contractName) << LOG_KV("address", contractAddress)
                << LOG_KV("version", contractVersion);
            gasPricer->appendOperation(InterfaceOpcode::Select, 1);
            getErrorCodeOut(
                callResult->mutableExecResult(), CODE_ADDRESS_AND_VERSION_EXIST, *codec);
            return;
        }
        cnsInfo.insert(
            std::make_pair(contractVersion, std::make_pair(contractAddress, contractAbi)));
        entry->setField(SYS_VALUE, asString(codec::scale::encode(cnsInfo)));
        table->setRow(contractName, entry.value());
    }
    else
    {
        // first insert, make new map to encode
        CNSInfoMap cnsInfo = {};
        cnsInfo.insert(
            std::make_pair(contractVersion, std::make_pair(contractAddress, contractAbi)));
        auto newEntry = table->newEntry();
        newEntry.importFields({asString(codec::scale::encode(cnsInfo))});
        table->setRow(contractName, std::move(newEntry));
    }
    gasPricer->updateMemUsed(1);
    gasPricer->appendOperation(InterfaceOpcode::Insert, 1);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("CNSPrecompiled") << LOG_DESC("insert successfully");
    getErrorCodeOut(callResult->mutableExecResult(), CODE_SUCCESS, *codec);
}

void CNSPrecompiled::selectByName(const std::shared_ptr<executor::TransactionExecutive>& _executive,
    bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
    const PrecompiledGas::Ptr& gasPricer)
{
    // selectByName(string) returns(string)
    std::string contractName;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, contractName);
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("CNSPrecompiled") << LOG_DESC("selectByName")
                           << LOG_KV("contractName", contractName);

    auto table = _executive->storage().openTable(SYS_CNS);
    gasPricer->appendOperation(InterfaceOpcode::OpenTable);
    if (!table)
    {
        table = _executive->storage().createTable(SYS_CNS, SYS_VALUE);
    }
    Json::Value CNSInfos(Json::arrayValue);
    auto entry = table->getRow(contractName);
    if (entry)
    {
        CNSInfoMap cnsInfoMap;
        auto&& out = asBytes(std::string(entry->getField(SYS_VALUE)));
        codec::scale::decode(cnsInfoMap, gsl::make_span(out));
        gasPricer->appendOperation(InterfaceOpcode::Select, cnsInfoMap.size());
        for (const auto& cnsInfo : cnsInfoMap)
        {
            Json::Value CNSInfo;
            CNSInfo[SYS_CNS_FIELD_NAME] = contractName;
            CNSInfo[SYS_CNS_FIELD_VERSION] = cnsInfo.first;
            CNSInfo[SYS_CNS_FIELD_ADDRESS] = cnsInfo.second.first;
            CNSInfo[SYS_CNS_FIELD_ABI] = cnsInfo.second.second;
            CNSInfos.append(CNSInfo);
        }
    }
    Json::FastWriter fastWriter;
    std::string str = fastWriter.write(CNSInfos);
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("CNSPrecompiled") << LOG_DESC("selectByName")
                           << LOG_KV("selectResult", str);
    callResult->setExecResult(codec->encode(str));
}

void CNSPrecompiled::selectByNameAndVersion(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const PrecompiledGas::Ptr& gasPricer)
{
    // selectByNameAndVersion(string,string) returns(address,string)
    // selectByNameAndVersion(string,string) returns(string,string) wasm
    std::string contractName, contractVersion;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, contractName, contractVersion);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("CNSPrecompiled") << LOG_DESC("selectByNameAndVersion")
                           << LOG_KV("contractName", contractName)
                           << LOG_KV("contractVersion", contractVersion);
    auto table = _executive->storage().openTable(SYS_CNS);
    gasPricer->appendOperation(InterfaceOpcode::OpenTable);
    if (!table)
    {
        table = _executive->storage().createTable(SYS_CNS, SYS_VALUE);
    }
    boost::trim(contractName);
    boost::trim(contractVersion);
    auto entry = table->getRow(contractName);
    auto notFindReturn = [&]() {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("CNSPrecompiled")
                               << LOG_DESC("can't get cns selectByNameAndVersion")
                               << LOG_KV("contractName", contractName)
                               << LOG_KV("contractVersion", contractVersion);
        if (blockContext->isWasm())
        {
            callResult->setExecResult(codec->encode(std::string(""), std::string("")));
        }
        else
        {
            callResult->setExecResult(codec->encode(Address(), std::string("")));
        }
    };
    if (!entry)
    {
        notFindReturn();
        return;
    }
    CNSInfoMap cnsInfoMap;
    auto&& out = asBytes(std::string(entry->getField(SYS_VALUE)));
    codec::scale::decode(cnsInfoMap, gsl::make_span(out));
    if (cnsInfoMap.find(contractVersion) == cnsInfoMap.end())
    {
        notFindReturn();
        return;
    }
    gasPricer->appendOperation(InterfaceOpcode::Select, cnsInfoMap.size());
    std::string contractAddress = cnsInfoMap.at(contractVersion).first;
    std::string abi = cnsInfoMap.at(contractVersion).second;
    if (blockContext->isWasm())
    {
        callResult->setExecResult(codec->encode(contractAddress, abi));
    }
    else
    {
        callResult->setExecResult(codec->encode(toAddress(contractAddress), abi));
    }
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("CNSPrecompiled") << LOG_DESC("selectByNameAndVersion")
                           << LOG_KV("contractAddress", contractAddress) << LOG_KV("abi", abi);
}

void CNSPrecompiled::getContractAddress(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const PrecompiledGas::Ptr& gasPricer)
{
    // getContractAddress(string,string) returns(address)
    // getContractAddress(string,string) returns(string) wasm
    std::string contractName, contractVersion;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, contractName, contractVersion);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("CNSPrecompiled") << LOG_DESC("getContractAddress")
                           << LOG_KV("contractName", contractName)
                           << LOG_KV("contractVersion", contractVersion);
    auto table = _executive->storage().openTable(SYS_CNS);
    gasPricer->appendOperation(InterfaceOpcode::OpenTable);
    if (!table)
    {
        table = _executive->storage().createTable(SYS_CNS, SYS_VALUE);
    }
    boost::trim(contractName);
    boost::trim(contractVersion);
    auto entry = table->getRow(contractName);
    auto notFindReturn = [&]() {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("CNSPrecompiled")
                               << LOG_DESC("can't get cns selectByNameAndVersion")
                               << LOG_KV("contractName", contractName)
                               << LOG_KV("contractVersion", contractVersion);
        if (blockContext->isWasm())
        {
            callResult->setExecResult(codec->encode(std::string(""), std::string("")));
        }
        else
        {
            callResult->setExecResult(codec->encode(Address(), std::string("")));
        }
    };
    if (!entry)
    {
        notFindReturn();
        return;
    }
    CNSInfoMap cnsInfoMap;
    auto&& out = asBytes(std::string(entry->getField(SYS_VALUE)));
    codec::scale::decode(cnsInfoMap, gsl::make_span(out));
    if (cnsInfoMap.find(contractVersion) == cnsInfoMap.end())
    {
        notFindReturn();
        return;
    }
    gasPricer->appendOperation(InterfaceOpcode::Select, cnsInfoMap.size());
    std::string contractAddress = cnsInfoMap.at(contractVersion).first;
    if (blockContext->isWasm())
    {
        callResult->setExecResult(codec->encode(contractAddress));
    }
    else
    {
        callResult->setExecResult(codec->encode(toAddress(contractAddress)));
    }
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("CNSPrecompiled") << LOG_DESC("getContractAddress")
                           << LOG_KV("contractAddress", contractAddress);
}