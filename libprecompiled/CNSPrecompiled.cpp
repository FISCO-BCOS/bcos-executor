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
#include <bcos-framework/libcodec/abi/ContractABICodec.h>

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::precompiled;
using namespace bcos::protocol;

const char* const CNS_METHOD_INS_STR4 = "insert(string,string,Address,string)";
const char* const CNS_METHOD_SLT_STR = "selectByName(string)";
const char* const CNS_METHOD_SLT_STR2 = "selectByNameAndVersion(string,string)";
const char* const CNS_METHOD_GET_CONTRACT_ADDRESS = "getContractAddress(string,string)";

CNSPrecompiled::CNSPrecompiled()
{
    name2Selector[CNS_METHOD_INS_STR4] = getFuncSelector(CNS_METHOD_INS_STR4);
    name2Selector[CNS_METHOD_SLT_STR] = getFuncSelector(CNS_METHOD_SLT_STR);
    name2Selector[CNS_METHOD_SLT_STR2] = getFuncSelector(CNS_METHOD_SLT_STR2);
    name2Selector[CNS_METHOD_GET_CONTRACT_ADDRESS] =
        getFuncSelector(CNS_METHOD_GET_CONTRACT_ADDRESS);
}


std::string CNSPrecompiled::toString()
{
    return "CNS";
}

// check param of the cns
bool CNSPrecompiled::checkCNSParam(ExecutiveContext::Ptr _context, Address const& _contractAddress,
    std::string const& _contractName, std::string const& _contractAbi)
{
    try
    {
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

    // check the length of the key
    checkLengthValidate(
        _contractName, CNS_CONTRACT_NAME_MAX_LENGTH, CODE_TABLE_KEY_VALUE_LENGTH_OVERFLOW);
    // check the length of the field value
    // (since the contract version length will be checked, here no need to check _contractVersion)
    checkLengthValidate(
        _contractAbi, USER_TABLE_FIELD_VALUE_MAX_LENGTH, CODE_TABLE_FIELD_VALUE_LENGTH_OVERFLOW);
    return true;
}

PrecompiledExecResult::Ptr CNSPrecompiled::call(
    std::shared_ptr<executor::ExecutiveContext> _context, bytesConstRef _param,
    const std::string& _origin, const std::string&, u256& _remainGas)
{
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("CNSPrecompiled") << LOG_DESC("call")
                           << LOG_KV("param", *toHexString(_param));

    // parse function name
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);

    bcos::codec::abi::ContractABICodec abi(nullptr);
    auto callResult = std::make_shared<PrecompiledExecResult>();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();

    gasPricer->setMemUsed(_param.size());

    if (func == name2Selector[CNS_METHOD_INS_STR4])
    {
        // insert(name, version, address, abi), 4 fields in table, the key of table is name field
        std::string contractName, contractVersion, contractAbi;
        Address contractAddress;
        abi.abiOut(data, contractName, contractVersion, contractAddress, contractAbi);

        auto table = _context->getTableFactory()->openTable(SYS_CNS);
        gasPricer->appendOperation(InterfaceOpcode::OpenTable);

        bool isValid = checkCNSParam(_context, contractAddress, contractName, contractAbi);
        // check exist or not
        bool exist = false;
        auto entry = table->getRow(contractName);
        // Note: The selection here is only used as an internal logical judgment,
        // so only calculate the computation gas
        gasPricer->appendOperation(InterfaceOpcode::Select, 1);

        if (!entry)
        {
            exist = false;
        }
        else if (entry->getField(SYS_CNS_FIELD_VERSION) == contractVersion)
        {
            exist = true;
        }
        int result = 0;
        if (contractVersion.size() > CNS_VERSION_MAX_LENGTH)
        {
            PRECOMPILED_LOG(ERROR)
                << LOG_BADGE("CNS") << LOG_DESC("version length overflow 128")
                << LOG_KV("contractName", contractName) << LOG_KV("address", contractAddress.hex())
                << LOG_KV("version", contractVersion);
            result = CODE_VERSION_LENGTH_OVERFLOW;
        }
        else if (exist)
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
                newEntry->setField(SYS_CNS_FIELD_NAME, contractName);
                newEntry->setField(SYS_CNS_FIELD_VERSION, contractVersion);
                newEntry->setField(SYS_CNS_FIELD_ABI, contractAbi);
                table->setRow(contractAddress.hex(), newEntry);
                auto commitResult = _context->getTableFactory()->commit();
                if (!commitResult.second ||
                    commitResult.second->errorCode() == CommonError::SUCCESS)
                {
                    gasPricer->updateMemUsed(entry->size() * commitResult.first);
                    gasPricer->appendOperation(
                        InterfaceOpcode::Insert, commitResult.first);
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
        getErrorCodeOut(callResult->mutableExecResult(), result);
        gasPricer->updateMemUsed(callResult->m_execResult.size());
        _remainGas -= gasPricer->calTotalGas();
    }
    else if (func == name2Selector[CNS_METHOD_SLT_STR])
    {
        // selectByName(string) returns(string)
        // Cursor is not considered.
        std::string contractName;
        abi.abiOut(data, contractName);
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
            auto entry = table->getRow(key);
            if (!entry)
            {
                continue;
            }
            if(entry->getField(SYS_CNS_FIELD_NAME) == contractName)
            {
                Json::Value CNSInfo;
                CNSInfo[SYS_CNS_FIELD_NAME] = entry->getField(SYS_CNS_FIELD_NAME);
                CNSInfo[SYS_CNS_FIELD_VERSION] = entry->getField(SYS_CNS_FIELD_VERSION);
                CNSInfo[SYS_CNS_FIELD_ADDRESS] = key;
                CNSInfo[SYS_CNS_FIELD_ABI] = entry->getField(SYS_CNS_FIELD_ABI);
                CNSInfos.append(CNSInfo);
            }
        }
        Json::FastWriter fastWriter;
        std::string str = fastWriter.write(CNSInfos);
        callResult->setExecResult(abi.abiIn("", str));
        gasPricer->updateMemUsed(callResult->m_execResult.size());
        _remainGas -= gasPricer->calTotalGas();
    }
    else if (func == name2Selector[CNS_METHOD_SLT_STR2])
    {
        // selectByNameAndVersion(string,string) returns(string)
        std::string contractName, contractVersion;
        abi.abiOut(data, contractName, contractVersion);
        auto table = _context->getTableFactory()->openTable(SYS_CNS);
        gasPricer->appendOperation(InterfaceOpcode::OpenTable);
        Json::Value CNSInfos(Json::arrayValue);

        // TODO: add traverse gas
        auto keys = table->getPrimaryKeys(nullptr);
        gasPricer->appendOperation(InterfaceOpcode::Select, keys.size());

        for (auto& key : keys)
        {
            auto entry = table->getRow(key);
            if (!entry)
            {
                continue;
            }
            if (entry->getField(SYS_CNS_FIELD_NAME) == contractName &&
                entry->getField(SYS_CNS_FIELD_VERSION) == contractVersion)
            {
                Json::Value CNSInfo;
                CNSInfo[SYS_CNS_FIELD_NAME] = contractName;
                CNSInfo[SYS_CNS_FIELD_VERSION] = entry->getField(SYS_CNS_FIELD_VERSION);
                CNSInfo[SYS_CNS_FIELD_ADDRESS] = entry->getField(SYS_CNS_FIELD_ADDRESS);
                CNSInfo[SYS_CNS_FIELD_ABI] = entry->getField(SYS_CNS_FIELD_ABI);
                CNSInfos.append(CNSInfo);
            }
        }
        Json::FastWriter fastWriter;
        std::string str = fastWriter.write(CNSInfos);
        callResult->setExecResult(abi.abiIn("", str));
        gasPricer->updateMemUsed(callResult->m_execResult.size());
        _remainGas -= gasPricer->calTotalGas();
    }
    else if (func == name2Selector[CNS_METHOD_GET_CONTRACT_ADDRESS])
    {  // getContractAddress(string,string) returns(address)
        std::string contractName, contractVersion;
        abi.abiOut(data, contractName, contractVersion);
        auto table = _context->getTableFactory()->openTable(SYS_CNS);
        gasPricer->appendOperation(InterfaceOpcode::OpenTable);

        Address ret;
        auto keys = table->getPrimaryKeys(nullptr);
        gasPricer->appendOperation(InterfaceOpcode::Select, keys.size());

        for (auto& key : keys)
        {
            auto entry = table->getRow(key);
            if (!entry)
            {
                continue;
            }
            if (entry->getField(SYS_CNS_FIELD_NAME) == contractName &&
                entry->getField(SYS_CNS_FIELD_VERSION) == contractVersion)
            {
                ret = Address(key);
            }
        }
        callResult->setExecResult(abi.abiIn("", ret));
        gasPricer->updateMemUsed(callResult->m_execResult.size());
        _remainGas -= gasPricer->calTotalGas();
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("CNSPrecompiled") << LOG_DESC("call undefined function")
                               << LOG_KV("func", func);
    }

    return callResult;
}