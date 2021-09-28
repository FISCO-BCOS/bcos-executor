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

CNSPrecompiled::CNSPrecompiled(crypto::Hash::Ptr _hashImpl) : Precompiled(_hashImpl)
{
    name2Selector[CNS_METHOD_INS_STR4] = getFuncSelector(CNS_METHOD_INS_STR4, _hashImpl);
    name2Selector[CNS_METHOD_SLT_STR] = getFuncSelector(CNS_METHOD_SLT_STR, _hashImpl);
    name2Selector[CNS_METHOD_SLT_STR2] = getFuncSelector(CNS_METHOD_SLT_STR2, _hashImpl);
    name2Selector[CNS_METHOD_GET_CONTRACT_ADDRESS] =
        getFuncSelector(CNS_METHOD_GET_CONTRACT_ADDRESS, _hashImpl);
}

std::string CNSPrecompiled::toString()
{
    return "CNS";
}

// check param of the cns
int CNSPrecompiled::checkCNSParam(BlockContext::Ptr _context, Address const& _contractAddress,
    std::string& _contractName, std::string& _contractVersion, std::string const& _contractAbi)
{
    boost::trim(_contractName);
    boost::trim(_contractVersion);
    // check the status of the contract(only print the error message to the log)
    std::string tableName = USER_APPS_PREFIX + _contractAddress.hex();
    ContractStatus contractStatus = getContractStatus(_context, tableName);

    if (contractStatus != ContractStatus::Available)
    {
        std::stringstream errorMessage;
        errorMessage << "CNS operation failed for ";
        switch (contractStatus)
        {
        case ContractStatus::Frozen:
            errorMessage << "\"" << _contractName
                         << "\" has been frozen, contractAddress = " << _contractAddress.hex();
            break;
        case ContractStatus::AddressNonExistent:
            errorMessage << "the contract \"" << _contractName << "\" with address "
                         << _contractAddress.hex() << " does not exist";
            break;
        case ContractStatus::NotContractAddress:
            errorMessage << "invalid address " << _contractAddress.hex()
                         << ", please make sure it's a contract address";
            break;
        default:
            errorMessage << "invalid contract \"" << _contractName << "\" with address "
                         << _contractAddress.hex()
                         << ", error code:" << std::to_string(contractStatus);
            break;
        }
        PRECOMPILED_LOG(INFO) << LOG_BADGE("CNSPrecompiled") << LOG_DESC(errorMessage.str())
                              << LOG_KV("contractAddress", _contractAddress.hex())
                              << LOG_KV("contractName", _contractName);
    }
    if (_contractVersion.size() > CNS_VERSION_MAX_LENGTH)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("CNSPrecompiled")
                               << LOG_DESC("version length overflow 128")
                               << LOG_KV("contractName", _contractName)
                               << LOG_KV("address", _contractAddress.hex())
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
    std::shared_ptr<executor::BlockContext> _context, bytesConstRef _param,
    const std::string& _origin, const std::string&)
{
    // parse function name
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("CNSPrecompiled") << LOG_DESC("call")
                           << LOG_KV("func", func);

    auto callResult = std::make_shared<PrecompiledExecResult>();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();

    gasPricer->setMemUsed(_param.size());

    if (func == name2Selector[CNS_METHOD_INS_STR4])
    {
        // insert(name, version, address, abi), 4 fields in table, the key of table is name field
        insert(_context, _origin, data, callResult, gasPricer);
    }
    else if (func == name2Selector[CNS_METHOD_SLT_STR])
    {
        // selectByName(string) returns(string)
        selectByName(_context, data, callResult, gasPricer);
    }
    else if (func == name2Selector[CNS_METHOD_SLT_STR2])
    {
        // selectByNameAndVersion(string,string) returns(address,string)
        selectByNameAndVersion(_context, data, callResult, gasPricer);
    }
    else if (func == name2Selector[CNS_METHOD_GET_CONTRACT_ADDRESS])
    {
        // getContractAddress(string,string) returns(address)
        getContractAddress(_context, data, callResult, gasPricer);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("CNSPrecompiled") << LOG_DESC("call undefined function")
                               << LOG_KV("func", func);
    }
    gasPricer->updateMemUsed(callResult->m_execResult.size());
    _remainGas -= gasPricer->calTotalGas();
    return callResult;
}

void CNSPrecompiled::insert(const std::shared_ptr<executor::BlockContext>& _context,
    const std::string& origin, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const PrecompiledGas::Ptr& gasPricer)
{
    // insert(name, version, address, abi), 4 fields in table, the key of table is name field
    std::string contractName, contractVersion, contractAbi;
    Address contractAddress;
    auto codec = std::make_shared<PrecompiledCodec>(_context->hashHandler(), _context->isWasm());
    codec->decode(data, contractName, contractVersion, contractAddress, contractAbi);

    int validCode =
        checkCNSParam(_context, contractAddress, contractName, contractVersion, contractAbi);

    auto table = _context->storage()->openTable(SYS_CNS);
    if (!table)
    {
        table = createTable(
            _context->storage(), SYS_CNS, SYS_CNS_FIELD_ADDRESS + "," + SYS_CNS_FIELD_ABI);
    }
    gasPricer->appendOperation(InterfaceOpcode::OpenTable);
    auto entry = table->getRow(contractName + "," + contractVersion);
    int result;
    if (entry)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("CNSPrecompiled")
                               << LOG_DESC("address and version exist")
                               << LOG_KV("contractName", contractName)
                               << LOG_KV("address", contractAddress.hex())
                               << LOG_KV("version", contractVersion);
        gasPricer->appendOperation(InterfaceOpcode::Select, 1);
        result = CODE_ADDRESS_AND_VERSION_EXIST;
    }
    else if (validCode < 0)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("CNSPrecompiled") << LOG_DESC("address invalid")
                               << LOG_KV("address", contractAddress.hex());
        result = validCode;
    }
    else
    {
        auto newEntry = table->newEntry();
        newEntry.setField(SYS_CNS_FIELD_ADDRESS, contractAddress.hex());
        newEntry.setField(SYS_CNS_FIELD_ABI, contractAbi);
        table->setRow(contractName + "," + contractVersion, newEntry);
        gasPricer->updateMemUsed(1);
        gasPricer->appendOperation(InterfaceOpcode::Insert, 1);
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("CNSPrecompiled") << LOG_DESC("insert successfully");
        result = CODE_SUCCESS;
    }
    getErrorCodeOut(callResult->mutableExecResult(), result, codec);
}

void CNSPrecompiled::selectByName(const std::shared_ptr<executor::BlockContext>& _context,
    bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
    const PrecompiledGas::Ptr& gasPricer)
{
    // selectByName(string) returns(string)
    std::string contractName;
    auto codec = std::make_shared<PrecompiledCodec>(_context->hashHandler(), _context->isWasm());
    codec->decode(data, contractName);
    auto table = _context->storage()->openTable(SYS_CNS);
    gasPricer->appendOperation(InterfaceOpcode::OpenTable);
    if (!table)
    {
        table = createTable(
            _context->storage(), SYS_CNS, SYS_CNS_FIELD_ADDRESS + "," + SYS_CNS_FIELD_ABI);
    }
    Json::Value CNSInfos(Json::arrayValue);
    auto keys = table->getPrimaryKeys(std::nullopt);
    // Note: Because the selected data has been returned as cnsInfo,
    // the memory is not updated here
    gasPricer->appendOperation(InterfaceOpcode::Set, keys.size());

    // TODO: add traverse gas
    for (auto& key : keys)
    {
        auto index = key.find(',');
        // "," must exist, and name,version must be trimmed
        std::pair<std::string, std::string> nameVersionPair{
            key.substr(0, index), key.substr(index + 1)};
        if (nameVersionPair.first == contractName)
        {
            auto entry = table->getRow(key);
            if (!entry)
            {
                continue;
            }
            Json::Value CNSInfo;
            CNSInfo[SYS_CNS_FIELD_NAME] = contractName;
            CNSInfo[SYS_CNS_FIELD_VERSION] = nameVersionPair.second;
            CNSInfo[SYS_CNS_FIELD_ADDRESS] = std::string(entry->getField(SYS_CNS_FIELD_ADDRESS));
            CNSInfo[SYS_CNS_FIELD_ABI] = std::string(entry->getField(SYS_CNS_FIELD_ABI));
            CNSInfos.append(CNSInfo);
        }
    }
    Json::FastWriter fastWriter;
    std::string str = fastWriter.write(CNSInfos);
    callResult->setExecResult(codec->encode(str));
}

void CNSPrecompiled::selectByNameAndVersion(const std::shared_ptr<executor::BlockContext>& _context,
    bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
    const PrecompiledGas::Ptr& gasPricer)
{
    // selectByNameAndVersion(string,string) returns(address,string)
    std::string contractName, contractVersion;
    auto codec = std::make_shared<PrecompiledCodec>(_context->hashHandler(), _context->isWasm());
    codec->decode(data, contractName, contractVersion);
    auto table = _context->storage()->openTable(SYS_CNS);
    gasPricer->appendOperation(InterfaceOpcode::OpenTable);
    if (!table)
    {
        table = createTable(
            _context->storage(), SYS_CNS, SYS_CNS_FIELD_ADDRESS + "," + SYS_CNS_FIELD_ABI);
    }
    Json::Value CNSInfos(Json::arrayValue);
    boost::trim(contractName);
    boost::trim(contractVersion);
    auto entry = table->getRow(contractName + "," + contractVersion);
    if (!entry)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("CNSPrecompiled")
                               << LOG_DESC("can't get cns selectByNameAndVersion")
                               << LOG_KV("contractName", contractName)
                               << LOG_KV("contractVersion", contractVersion);
        callResult->setExecResult(codec->encode(Address(), std::string("")));
    }
    else
    {
        gasPricer->appendOperation(InterfaceOpcode::Select, entry->capacityOfHashField());
        Address contractAddress = toAddress(std::string(entry->getField(SYS_CNS_FIELD_ADDRESS)));
        std::string abi = std::string(entry->getField(SYS_CNS_FIELD_ABI));
        callResult->setExecResult(codec->encode(contractAddress, abi));
    }
}

void CNSPrecompiled::getContractAddress(const std::shared_ptr<executor::BlockContext>& _context,
    bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
    const PrecompiledGas::Ptr& gasPricer)
{
    // getContractAddress(string,string) returns(address)
    std::string contractName, contractVersion;
    auto codec = std::make_shared<PrecompiledCodec>(_context->hashHandler(), _context->isWasm());
    codec->decode(data, contractName, contractVersion);
    auto table = _context->storage()->openTable(SYS_CNS);
    if (!table)
    {
        table = createTable(
            _context->storage(), SYS_CNS, SYS_CNS_FIELD_ADDRESS + "," + SYS_CNS_FIELD_ABI);
    }
    gasPricer->appendOperation(InterfaceOpcode::OpenTable);
    Json::Value CNSInfos(Json::arrayValue);
    boost::trim(contractName);
    boost::trim(contractVersion);
    auto entry = table->getRow(contractName + "," + contractVersion);
    if (!entry)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("CNSPrecompiled")
                               << LOG_DESC("can't get cns selectByNameAndVersion")
                               << LOG_KV("contractName", contractName)
                               << LOG_KV("contractVersion", contractVersion);
        callResult->setExecResult(codec->encode(Address()));
    }
    else
    {
        gasPricer->appendOperation(InterfaceOpcode::Select, entry->capacityOfHashField());
        Address contractAddress = toAddress(std::string(entry->getField(SYS_CNS_FIELD_ADDRESS)));
        callResult->setExecResult(codec->encode(contractAddress));
    }
}