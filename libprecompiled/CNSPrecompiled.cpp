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

const char* const CNS_METHOD_INS_STR4 = "insert(string,string,Address,string)";
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
bool CNSPrecompiled::checkCNSParam(BlockContext::Ptr _context, Address const& _contractAddress,
    std::string& _contractName, std::string& _contractVersion, std::string const& _contractAbi)
{
    try
    {
        boost::trim(_contractName);
        boost::trim(_contractVersion);
        // check the status of the contract(only print the error message to the log)
        std::string tableName = precompiled::getContractTableName(_contractAddress.hex());
        ContractStatus contractStatus = getContractStatus(_context, tableName);

        if (contractStatus != ContractStatus::Available)
        {
            std::stringstream errorMessage;
            errorMessage << "CNS operation failed for ";
            switch (contractStatus)
            {
            case ContractStatus::Invalid:
                errorMessage << "invalid contract \"" << _contractName
                             << "\", contractAddress = " << _contractAddress.hex();
                break;
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
    }
    catch (std::exception const& _e)
    {
        PRECOMPILED_LOG(WARNING) << LOG_BADGE("CNSPrecompiled")
                                 << LOG_DESC("check contract status exception")
                                 << LOG_KV("contractAddress", _contractAddress.hex())
                                 << LOG_KV("contractName", _contractName)
                                 << LOG_KV("e", boost::diagnostic_information(_e));
    }
    if (_contractVersion.size() > CNS_VERSION_MAX_LENGTH)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("CNSPrecompiled")
                               << LOG_DESC("version length overflow 128")
                               << LOG_KV("contractName", _contractName)
                               << LOG_KV("address", _contractAddress.hex())
                               << LOG_KV("version", _contractVersion);
        return false;
    }
    if (_contractVersion.find(',') != std::string::npos ||
        _contractName.find(',') != std::string::npos)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("CNSPrecompiled")
                               << LOG_DESC("version or name contains \",\"")
                               << LOG_KV("contractName", _contractName)
                               << LOG_KV("version", _contractVersion);
        return false;
    }
    // check the length of the key
    checkLengthValidate(
        _contractName, CNS_CONTRACT_NAME_MAX_LENGTH, CODE_TABLE_KEY_VALUE_LENGTH_OVERFLOW);
    // check the length of the field value
    checkLengthValidate(
        _contractAbi, USER_TABLE_FIELD_VALUE_MAX_LENGTH, CODE_TABLE_FIELD_VALUE_LENGTH_OVERFLOW);
    return true;
}

PrecompiledExecResult::Ptr CNSPrecompiled::call(std::shared_ptr<executor::BlockContext> _context,
    bytesConstRef _param, const std::string& _origin, const std::string&, u256& _remainGas)
{
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("CNSPrecompiled") << LOG_DESC("call")
                           << LOG_KV("param", *toHexString(_param));

    // parse function name
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);

    m_codec = std::make_shared<PrecompiledCodec>(_context->hashHandler(), _context->isWasm());
    auto callResult = std::make_shared<PrecompiledExecResult>();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();

    gasPricer->setMemUsed(_param.size());

    if (func == name2Selector[CNS_METHOD_INS_STR4])
    {
        // insert(name, version, address, abi), 4 fields in table, the key of table is name field
        std::string contractName, contractVersion, contractAbi;
        Address contractAddress;
        m_codec->decode(data, contractName, contractVersion, contractAddress, contractAbi);

        auto table = _context->getTableFactory()->openTable(SYS_CNS);
        gasPricer->appendOperation(InterfaceOpcode::OpenTable);
        bool isValid =
            checkCNSParam(_context, contractAddress, contractName, contractVersion, contractAbi);
        auto entry = table->getRow(contractName + "," + contractVersion);
        // check exist or not
        bool exist = (entry != nullptr);
        // Note: The selection here is only used as an internal logical judgment,
        // so only calculate the computation gas
        gasPricer->appendOperation(InterfaceOpcode::Select, 1);
        int result;
        if (exist)
        {
            PRECOMPILED_LOG(ERROR)
                << LOG_BADGE("CNSPrecompiled") << LOG_DESC("address and version exist")
                << LOG_KV("contractName", contractName) << LOG_KV("address", contractAddress.hex())
                << LOG_KV("version", contractVersion);
            result = CODE_ADDRESS_AND_VERSION_EXIST;
        }
        else if (!isValid)
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("CNSPrecompiled") << LOG_DESC("address invalid")
                                   << LOG_KV("address", contractAddress.hex());
            result = CODE_ADDRESS_INVALID;
        }
        else
        {
            if (_context->getTableFactory()->checkAuthority(SYS_CNS, _origin))
            {
                auto newEntry = table->newEntry();
                newEntry->setField(SYS_CNS_FIELD_ADDRESS, contractAddress.hex());
                newEntry->setField(SYS_CNS_FIELD_ABI, contractAbi);
                table->setRow(contractName + "," + contractVersion, newEntry);
                auto commitResult = _context->getTableFactory()->commit();
                if (!commitResult.second ||
                    commitResult.second->errorCode() == CommonError::SUCCESS)
                {
                    gasPricer->updateMemUsed(entry->size() * commitResult.first);
                    gasPricer->appendOperation(InterfaceOpcode::Insert, commitResult.first);
                    PRECOMPILED_LOG(DEBUG)
                        << LOG_BADGE("CNSPrecompiled") << LOG_DESC("insert successfully");
                    result = commitResult.first;
                }
                else
                {
                    PRECOMPILED_LOG(DEBUG)
                        << LOG_BADGE("CNSPrecompiled") << LOG_DESC("insert failed");
                    // TODO: use unify error code
                    result = -1;
                }
            }
            else
            {
                PRECOMPILED_LOG(DEBUG)
                    << LOG_BADGE("CNSPrecompiled") << LOG_DESC("permission denied");
                // TODO: use unify error code
                result = -1;
            }
        }
        getErrorCodeOut(callResult->mutableExecResult(), result, m_codec);
    }
    else if (func == name2Selector[CNS_METHOD_SLT_STR])
    {
        // selectByName(string) returns(string)
        std::string contractName;
        m_codec->decode(data, contractName);
        auto table = _context->getTableFactory()->openTable(SYS_CNS);
        gasPricer->appendOperation(InterfaceOpcode::OpenTable);

        Json::Value CNSInfos(Json::arrayValue);
        auto keys = table->getPrimaryKeys(nullptr);
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
                CNSInfo[SYS_CNS_FIELD_ADDRESS] = entry->getField(SYS_CNS_FIELD_ADDRESS);
                CNSInfo[SYS_CNS_FIELD_ABI] = entry->getField(SYS_CNS_FIELD_ABI);
                CNSInfos.append(CNSInfo);
            }
        }
        Json::FastWriter fastWriter;
        std::string str = fastWriter.write(CNSInfos);
        callResult->setExecResult(m_codec->encode(str));
    }
    else if (func == name2Selector[CNS_METHOD_SLT_STR2])
    {
        // selectByNameAndVersion(string,string) returns(address,string)
        std::string contractName, contractVersion;
        m_codec->decode(data, contractName, contractVersion);
        auto table = _context->getTableFactory()->openTable(SYS_CNS);
        gasPricer->appendOperation(InterfaceOpcode::OpenTable);
        Json::Value CNSInfos(Json::arrayValue);
        boost::trim(contractName);
        boost::trim(contractVersion);
        auto entry = table->getRow(contractName + "," + contractVersion);
        gasPricer->appendOperation(InterfaceOpcode::Select, entry->capacityOfHashField());
        if (!entry)
        {
            PRECOMPILED_LOG(DEBUG)
                << LOG_BADGE("CNSPrecompiled") << LOG_DESC("can't get cns selectByNameAndVersion")
                << LOG_KV("contractName", contractName)
                << LOG_KV("contractVersion", contractVersion);
            callResult->setExecResult(m_codec->encode((int)CODE_ADDRESS_AND_VERSION_EXIST));
        }
        else
        {
            Address contractAddress = toAddress(entry->getField(SYS_CNS_FIELD_ADDRESS));
            std::string abi = entry->getField(SYS_CNS_FIELD_ABI);
            callResult->setExecResult(m_codec->encode(contractAddress, abi));
        }
    }
    else if (func == name2Selector[CNS_METHOD_GET_CONTRACT_ADDRESS])
    {  // getContractAddress(string,string) returns(address)
        std::string contractName, contractVersion;
        m_codec->decode(data, contractName, contractVersion);
        auto table = _context->getTableFactory()->openTable(SYS_CNS);
        gasPricer->appendOperation(InterfaceOpcode::OpenTable);
        Json::Value CNSInfos(Json::arrayValue);
        boost::trim(contractName);
        boost::trim(contractVersion);
        auto entry = table->getRow(contractName + "," + contractVersion);
        gasPricer->appendOperation(InterfaceOpcode::Select, entry->capacityOfHashField());
        if (!entry)
        {
            PRECOMPILED_LOG(DEBUG)
                << LOG_BADGE("CNSPrecompiled") << LOG_DESC("can't get cns selectByNameAndVersion")
                << LOG_KV("contractName", contractName)
                << LOG_KV("contractVersion", contractVersion);
            callResult->setExecResult(m_codec->encode((int)CODE_ADDRESS_AND_VERSION_EXIST));
        }
        else
        {
            Address contractAddress = toAddress(entry->getField(SYS_CNS_FIELD_ADDRESS));
            callResult->setExecResult(m_codec->encode(contractAddress));
        }
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