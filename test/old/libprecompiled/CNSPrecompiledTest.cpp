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
 * @date 2021-06-03
 */

#include "libprecompiled/CNSPrecompiled.h"
#include "PreCompiledFixture.h"
#include "libprecompiled/Common.h"
#include "state/State.h"
#include <bcos-framework/interfaces/storage/TableInterface.h>
#include <bcos-framework/testutils/TestPromptFixture.h>
#include <json/json.h>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::ledger;

namespace bcos::test
{
class CNSPrecompiledFixture : public PrecompiledFixture
{
public:
    CNSPrecompiledFixture()
    {
        cnsPrecompiled = std::make_shared<CNSPrecompiled>(hashImpl);
        codec = std::make_shared<PrecompiledCodec>(hashImpl, false);
        setIsWasm(false);
    }

    virtual ~CNSPrecompiledFixture() {}

    void initContractTable()
    {
        auto tableFactory = context->getTableFactory();
        tableFactory->createTable("c_420f853b49838bd3e9466c85a4cc3428c960dde2", SYS_KEY, SYS_VALUE);

        auto table =
            context->getTableFactory()->openTable("c_420f853b49838bd3e9466c85a4cc3428c960dde2");
        auto entry = table->newEntry();
        entry->setField(SYS_VALUE, "");
        table->setRow(executor::ACCOUNT_CODE_HASH, entry);
        tableFactory->commit();
    }

    void initContractCodeHash()
    {
        auto table =
            context->getTableFactory()->openTable("c_420f853b49838bd3e9466c85a4cc3428c960dde2");
        auto entry = table->newEntry();
        entry->setField(SYS_VALUE, "123456");
        table->setRow(executor::ACCOUNT_CODE_HASH, entry);

        auto entry2 = table->newEntry();
        entry2->setField(SYS_VALUE, "true");
        table->setRow(executor::ACCOUNT_FROZEN, entry2);
        context->getTableFactory()->commit();
    }

    void initContractFrozen()
    {
        auto table =
            context->getTableFactory()->openTable("c_420f853b49838bd3e9466c85a4cc3428c960dde2");
        auto entry = table->newEntry();
        entry->setField(SYS_VALUE, "false");
        table->setRow(executor::ACCOUNT_FROZEN, entry);
        context->getTableFactory()->commit();
    }

    CNSPrecompiled::Ptr cnsPrecompiled;
};

BOOST_FIXTURE_TEST_SUITE(precompiledCNSTest, CNSPrecompiledFixture)

BOOST_AUTO_TEST_CASE(insertTest)
{
    std::string contractName = "Ok";
    std::string contractVersion = "1.0";
    Address contractAddress = Address("0x420f853b49838bd3e9466c85a4cc3428c960dde2");
    std::string contractAbi =
        "[{\"constant\":false,\"inputs\":[{\"name\":"
        "\"num\",\"type\":\"uint256\"}],\"name\":"
        "\"trans\",\"outputs\":[],\"payable\":false,"
        "\"type\":\"function\"},{\"constant\":true,"
        "\"inputs\":[],\"name\":\"get\",\"outputs\":[{"
        "\"name\":\"\",\"type\":\"uint256\"}],"
        "\"payable\":false,\"type\":\"function\"},{"
        "\"inputs\":[],\"payable\":false,\"type\":"
        "\"constructor\"}]";

    // insert overflow
    std::string overflowVersion130 =
        "012345678901234567890123456789012345678901234567890123456789"
        "0123456789012345678901234567890123456789012345678901234567890123456789";

    bytes in = codec->encodeWithSig("insert(string,string,address,string)", contractName,
        contractVersion, contractAddress, contractAbi);
    auto callResult = cnsPrecompiled->call(context, bytesConstRef(&in), "", "", gas);
    bytes out = callResult->execResult();
    context->getTableFactory()->commit();
    // query
    auto table = memoryTableFactory->openTable(SYS_CNS);
    auto entry = table->getRow(contractName + "," + contractVersion);
    BOOST_TEST(entry != nullptr);

    // insert again with same item
    callResult = cnsPrecompiled->call(context, bytesConstRef(&in), "", "", gas);
    out = callResult->execResult();
    s256 errCode;
    codec->decode(ref(out), errCode);
    BOOST_TEST(errCode == s256((int)CODE_ADDRESS_AND_VERSION_EXIST));

    bytes in2 = codec->encodeWithSig("insert(string,string,address,string)", contractName,
        overflowVersion130, contractAddress, contractAbi);
    callResult = (cnsPrecompiled->call(context, bytesConstRef(&in2), "", "", gas));
    bytes out2 = callResult->execResult();
    codec->decode(ref(out2), errCode);
    BOOST_TEST(errCode == s256((int)CODE_VERSION_LENGTH_OVERFLOW));

    // insert new item with same name, address and abi
    contractVersion = "2.0";
    in2 = codec->encodeWithSig("insert(string,string,address,string)", contractName,
        contractVersion, contractAddress, contractAbi);
    callResult = cnsPrecompiled->call(context, bytesConstRef(&in2), "", "", gas);
    out = callResult->execResult();
    context->getTableFactory()->commit();
    // query
    auto table2 = memoryTableFactory->openTable(SYS_CNS);
    auto entry2 = table2->getRow(contractName + "," + contractVersion);
    BOOST_TEST(entry2 != nullptr);

    initContractTable();
    contractVersion = "3.0";
    in2 = codec->encodeWithSig("insert(string,string,address,string)", contractName,
        contractVersion, contractAddress, contractAbi);
    callResult = cnsPrecompiled->call(context, bytesConstRef(&in2), "", "", gas);
    out = callResult->execResult();

    initContractCodeHash();
    contractVersion = "4.0";
    in2 = codec->encodeWithSig("insert(string,string,address,string)", contractName,
        contractVersion, contractAddress, contractAbi);
    callResult = cnsPrecompiled->call(context, bytesConstRef(&in2), "", "", gas);
    out = callResult->execResult();

    initContractFrozen();
    contractVersion = "5.0";
    in2 = codec->encodeWithSig("insert(string,string,address,string)", contractName,
        contractVersion, contractAddress, contractAbi);
    callResult = cnsPrecompiled->call(context, bytesConstRef(&in2), "", "", gas);
    out = callResult->execResult();
}

BOOST_AUTO_TEST_CASE(selectTest)
{
    // first insert
    std::string contractName = "Ok";
    std::string contractVersion = "1.0";
    Address contractAddress = Address("0x420f853b49838bd3e9466c85a4cc3428c960dde2");
    std::string contractAbi =
        "[{\"constant\":false,\"inputs\":[{\"name\":"
        "\"num\",\"type\":\"uint256\"}],\"name\":"
        "\"trans\",\"outputs\":[],\"payable\":false,"
        "\"type\":\"function\"},{\"constant\":true,"
        "\"inputs\":[],\"name\":\"get\",\"outputs\":[{"
        "\"name\":\"\",\"type\":\"uint256\"}],"
        "\"payable\":false,\"type\":\"function\"},{"
        "\"inputs\":[],\"payable\":false,\"type\":"
        "\"constructor\"}]";

    // select not exist keys
    bytes in = codec->encodeWithSig("selectByName(string)", contractName);
    auto callResult = cnsPrecompiled->call(context, bytesConstRef(&in), "", "", gas);
    bytes out = callResult->execResult();
    std::string retStr;
    codec->decode(&out, retStr);
    BOOST_CHECK(!retStr.empty());

    in = codec->encodeWithSig("insert(string,string,address,string)", contractName, contractVersion,
        contractAddress, contractAbi);
    callResult = cnsPrecompiled->call(context, bytesConstRef(&in), "", "", gas);
    out = callResult->execResult();

    // insert new item with same name, address and abi
    contractVersion = "2.0";
    in = codec->encodeWithSig("insert(string,string,address,string)", contractName, contractVersion,
        contractAddress, contractAbi);
    callResult = cnsPrecompiled->call(context, bytesConstRef(&in), "", "", gas);
    out = callResult->execResult();

    // select existing keys
    in = codec->encodeWithSig("selectByName(string)", contractName);
    callResult = cnsPrecompiled->call(context, bytesConstRef(&in), "", "", gas);
    out = callResult->execResult();
    codec->decode(&out, retStr);

    BCOS_LOG(TRACE) << "select result:" << retStr;
    Json::Value retJson;
    Json::Reader reader;
    BOOST_TEST(reader.parse(retStr, retJson) == true);
    BOOST_TEST(retJson.size() == 2);

    // getContractAddress
    in = codec->encodeWithSig("getContractAddress(string,string)", contractName, contractVersion);
    callResult = cnsPrecompiled->call(context, bytesConstRef(&in), "", "", gas);
    out = callResult->execResult();
    Address ret;
    codec->decode(&out, ret);
    BOOST_TEST(ret == contractAddress);

    // get no existing key
    in = codec->encodeWithSig(
        "getContractAddress(string,string)", std::string("Ok2"), contractVersion);
    callResult = cnsPrecompiled->call(context, bytesConstRef(&in), "", "", gas);
    out = callResult->execResult();
    codec->decode(&out, ret);
    BOOST_TEST(ret != contractAddress);

    // select no existing keys
    in = codec->encodeWithSig("selectByName(string)", std::string("Ok2"));
    callResult = cnsPrecompiled->call(context, bytesConstRef(&in), "", "", gas);
    out = callResult->execResult();
    codec->decode(&out, retStr);
    BCOS_LOG(TRACE) << "select result:" << retStr;
    BOOST_TEST(reader.parse(retStr, retJson) == true);
    BOOST_TEST(retJson.size() == 0);

    // select existing keys and version
    in = codec->encodeWithSig(
        "selectByNameAndVersion(string,string)", contractName, contractVersion);
    callResult = cnsPrecompiled->call(context, bytesConstRef(&in), "", "", gas);
    out = callResult->execResult();
    std::string abi;

    codec->decode(&out, ret, abi);
    BCOS_LOG(TRACE) << "select result: address:" << ret.hex() << " abi:" << abi;
    BOOST_TEST(abi == contractAbi);
    BOOST_TEST(ret == contractAddress);

    // select no existing keys and version
    in = codec->encodeWithSig(
        "selectByNameAndVersion(string,string)", contractName, std::string("3.0"));
    callResult = cnsPrecompiled->call(context, bytesConstRef(&in), "", "", gas);
    out = callResult->execResult();
    abi = "";
    codec->decode(&out, ret, abi);
    BCOS_LOG(TRACE) << "select result: address:" << ret.hex() << " abi:" << abi;
    BOOST_TEST(abi != contractAbi);
    BOOST_TEST(ret != contractAddress);

    in = codec->encodeWithSig(
        "selectByNameAndVersion(string,string)", std::string("Ok2"), contractVersion);
    callResult = cnsPrecompiled->call(context, bytesConstRef(&in), "", "", gas);
    out = callResult->execResult();
    codec->decode(&out, ret, abi);
    BCOS_LOG(TRACE) << "select result: address:" << ret.hex() << " abi:" << abi;
    BOOST_TEST(abi != contractAbi);
    BOOST_TEST(ret != contractAddress);
}

BOOST_AUTO_TEST_CASE(errFunc)
{
    BOOST_TEST(cnsPrecompiled->toString() == "CNS");

    bytes in = codec->encodeWithSig("insert(string)", std::string("test"));
    auto callResult = cnsPrecompiled->call(context, bytesConstRef(&in), "", "", gas);
    bytes out = callResult->execResult();
    s256 errorCode;
    codec->decode(&out, errorCode);
    BOOST_TEST(errorCode == 0);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test