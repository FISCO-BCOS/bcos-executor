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
 * @file ConfigPrecompiledTest.cpp
 * @author: kyonRay
 * @date 2021-06-22
 */

#include "PreCompiledFixture.h"
#include "precompiled/ConsensusPrecompiled.h"
#include "precompiled/ParallelConfigPrecompiled.h"
#include "precompiled/SystemConfigPrecompiled.h"
#include <bcos-framework/testutils/TestPromptFixture.h>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;

namespace bcos::test
{
class ConfigPrecompiledFixture : public PrecompiledFixture
{
public:
    ConfigPrecompiledFixture()
    {
        parallelConfigPrecompiled = std::make_shared<ParallelConfigPrecompiled>(hashImpl);
        systemConfigPrecompiled = std::make_shared<SystemConfigPrecompiled>(hashImpl);
        consensusPrecompiled = std::make_shared<ConsensusPrecompiled>(hashImpl);
    }

    void initEvmEnv() { setIsWasm(false); }

    void initWasmEnv() { setIsWasm(true); }

    virtual ~ConfigPrecompiledFixture() {}

    Address contractAddress = Address("0x420f853b49838bd3e9466c85a4cc3428c960dde2");
    ParallelConfigPrecompiled::Ptr parallelConfigPrecompiled;
    SystemConfigPrecompiled::Ptr systemConfigPrecompiled;
    ConsensusPrecompiled::Ptr consensusPrecompiled;
    int addressCount = 0x10000;
};
BOOST_FIXTURE_TEST_SUITE(precompiledTableTest, ConfigPrecompiledFixture)

BOOST_AUTO_TEST_CASE(paraConfig_test)
{
    initEvmEnv();
    {
        bytes in = codec->encodeWithSig("registerParallelFunctionInternal(address,string,uint256)",
            contractAddress, std::string("get()"), u256(1));
        auto callResult = parallelConfigPrecompiled->call(tempExecutive, bytesConstRef(&in), "", "");
        bytes out = callResult->execResult();
        u256 code;
        codec->decode(&out, code);
        BOOST_CHECK(code == u256(0));

        auto paraConfig = parallelConfigPrecompiled->getParallelConfig(
            tempExecutive, contractAddress.hex(), getFuncSelector("get()", hashImpl), "");
        BOOST_CHECK(paraConfig->functionName == "get()");

        in = codec->encodeWithSig("unregisterParallelFunctionInternal(address,string)",
            contractAddress, std::string("get()"));
        callResult = parallelConfigPrecompiled->call(tempExecutive, bytesConstRef(&in), "", "");
        out = callResult->execResult();
        codec->decode(&out, code);
        BOOST_CHECK(code == u256(0));

        in = codec->encodeWithSig("undefined(address,string)");
        parallelConfigPrecompiled->call(tempExecutive, bytesConstRef(&in), "", "");
    }

    initWasmEnv();
    {
        bytes in = codec->encodeWithSig("registerParallelFunctionInternal(string,string,uint256)",
            contractAddress.hex(), std::string("get()"), u256(1));
        auto callResult = parallelConfigPrecompiled->call(tempExecutive, bytesConstRef(&in), "", "");
        bytes out = callResult->execResult();
        u256 code;
        codec->decode(&out, code);
        BOOST_CHECK(code == u256(0));

        auto paraConfig = parallelConfigPrecompiled->getParallelConfig(
            tempExecutive, contractAddress.hex(), getFuncSelector("get()", hashImpl), "");
        BOOST_CHECK(paraConfig->functionName == "get()");

        in = codec->encodeWithSig("unregisterParallelFunctionInternal(string,string)",
            contractAddress.hex(), std::string("get()"));
        callResult = parallelConfigPrecompiled->call(tempExecutive, bytesConstRef(&in), "", "");
        out = callResult->execResult();
        codec->decode(&out, code);
        BOOST_CHECK(code == u256(0));
    }
}

BOOST_AUTO_TEST_CASE(sysConfig_test)
{
    initEvmEnv();

    BOOST_CHECK(systemConfigPrecompiled->toString() == "SystemConfig");

    bytes in = codec->encodeWithSig(
        "setValueByKey(string,string)", ledger::SYSTEM_KEY_TX_GAS_LIMIT, std::string("1000000"));
    auto callResult = systemConfigPrecompiled->call(tempExecutive, bytesConstRef(&in), "", "");
    bytes out = callResult->execResult();
    u256 code;
    codec->decode(&out, code);
    BOOST_CHECK(code == u256(0));

    in = codec->encodeWithSig("getValueByKey(string)", ledger::SYSTEM_KEY_TX_GAS_LIMIT);
    callResult = systemConfigPrecompiled->call(tempExecutive, bytesConstRef(&in), "", "");
    out = callResult->execResult();
    std::string value;
    u256 number;
    codec->decode(&out, value, number);
    BOOST_CHECK(value == "1000000");

    in = codec->encodeWithSig(
        "setValueByKey(string,string)", ledger::SYSTEM_KEY_TX_COUNT_LIMIT, std::string("1000"));
    callResult = systemConfigPrecompiled->call(tempExecutive, bytesConstRef(&in), "", "");
    out = callResult->execResult();
    codec->decode(&out, code);
    BOOST_CHECK(code == u256(0));

    in = codec->encodeWithSig(
        "setValueByKey(string,string)", ledger::SYSTEM_KEY_TX_COUNT_LIMIT, std::string("error"));
    callResult = systemConfigPrecompiled->call(tempExecutive, bytesConstRef(&in), "", "");
    s256 errorCode;
    out = callResult->execResult();
    codec->decode(&out, errorCode);
    BOOST_CHECK(errorCode == s256((int)CODE_INVALID_CONFIGURATION_VALUES));

    in = codec->encodeWithSig(
        "setValueByKey(string,string)", ledger::SYSTEM_KEY_CONSENSUS_TIMEOUT, std::string("1000"));
    callResult = systemConfigPrecompiled->call(tempExecutive, bytesConstRef(&in), "", "");
    out = callResult->execResult();
    codec->decode(&out, code);
    BOOST_CHECK(code == u256(0));

    in = codec->encodeWithSig(
        "setValueByKey(string,string)", ledger::SYSTEM_KEY_CONSENSUS_TIMEOUT, std::string("error"));
    callResult = systemConfigPrecompiled->call(tempExecutive, bytesConstRef(&in), "", "");
    out = callResult->execResult();
    codec->decode(&out, errorCode);
    BOOST_CHECK(errorCode == s256((int)CODE_INVALID_CONFIGURATION_VALUES));

    in = codec->encodeWithSig(
        "setValueByKey(string,string)", std::string("errorKey"), std::string("1000"));
    callResult = systemConfigPrecompiled->call(tempExecutive, bytesConstRef(&in), "", "");
    out = callResult->execResult();
    codec->decode(&out, code);
    BOOST_CHECK(code == u256((int)CODE_INVALID_CONFIGURATION_VALUES));
}

BOOST_AUTO_TEST_CASE(consensus_test)
{
    initWasmEnv();
    std::string node1;
    std::string node2;
    for (int i = 0; i < 128; ++i)
    {
        node1 += "1";
        node2 += "2";
    }

    context->storage()->createTable(ledger::SYS_CONSENSUS, "type,weight,enable_number");

    // node id too short
    bytes in = codec->encodeWithSig("addSealer(string,uint256)", std::string("111111"), u256(1));
    auto callResult = consensusPrecompiled->call(tempExecutive, bytesConstRef(&in), "", "");
    bytes out = callResult->execResult();
    s256 errorCode;
    codec->decode(&out, errorCode);
    BOOST_CHECK(errorCode == s256((int)CODE_INVALID_NODE_ID));

    // node id too short
    in = codec->encodeWithSig("addObserver(string)", std::string("111111"));
    callResult = consensusPrecompiled->call(tempExecutive, bytesConstRef(&in), "", "");
    out = callResult->execResult();
    codec->decode(&out, errorCode);
    BOOST_CHECK(errorCode == s256((int)CODE_INVALID_NODE_ID));

    // node id too short
    in = codec->encodeWithSig("remove(string)", std::string("111111"));
    callResult = consensusPrecompiled->call(tempExecutive, bytesConstRef(&in), "", "");
    out = callResult->execResult();
    codec->decode(&out, errorCode);
    BOOST_CHECK(errorCode == s256((int)CODE_INVALID_NODE_ID));

    // node id too short
    in = codec->encodeWithSig("setWeight(string,uint256)", std::string("111111"), u256(11));
    callResult = consensusPrecompiled->call(tempExecutive, bytesConstRef(&in), "", "");
    out = callResult->execResult();
    codec->decode(&out, errorCode);
    BOOST_CHECK(errorCode == s256((int)CODE_INVALID_NODE_ID));

    // add sealer node1
    in = codec->encodeWithSig("addSealer(string,uint256)", node1, u256(1));
    callResult = consensusPrecompiled->call(tempExecutive, bytesConstRef(&in), "", "");
    out = callResult->execResult();
    u256 code;
    codec->decode(&out, code);
    BOOST_CHECK(code == 0u);

    // add observer node2
    in = codec->encodeWithSig("addObserver(string)", node2);
    callResult = consensusPrecompiled->call(tempExecutive, bytesConstRef(&in), "", "");
    out = callResult->execResult();
    codec->decode(&out, code);
    BOOST_CHECK(code == 0u);

    // turn last sealer to observer
    in = codec->encodeWithSig("addObserver(string)", node1);
    callResult = consensusPrecompiled->call(tempExecutive, bytesConstRef(&in), "", "");
    out = callResult->execResult();
    codec->decode(&out, errorCode);
    BOOST_CHECK(errorCode == s256((int)CODE_LAST_SEALER));

    // remove last sealer
    in = codec->encodeWithSig("remove(string)", node1);
    callResult = consensusPrecompiled->call(tempExecutive, bytesConstRef(&in), "", "");
    out = callResult->execResult();
    codec->decode(&out, errorCode);
    BOOST_CHECK(errorCode == s256((int)CODE_LAST_SEALER));

    // remove observer
    in = codec->encodeWithSig("remove(string)", node2);
    callResult = consensusPrecompiled->call(tempExecutive, bytesConstRef(&in), "", "");
    out = callResult->execResult();
    codec->decode(&out, code);
    BOOST_CHECK(code == 0u);

    // set weigh to not exist node2
    in = codec->encodeWithSig("setWeight(string,uint256)", node2, u256(123));
    callResult = consensusPrecompiled->call(tempExecutive, bytesConstRef(&in), "", "");
    out = callResult->execResult();
    codec->decode(&out, errorCode);
    BOOST_CHECK(errorCode == s256((int)CODE_NODE_NOT_EXIST));

    // set an invalid weight(0) to node
    in = codec->encodeWithSig("setWeight(string,uint256)", node1, u256(0));
    callResult = consensusPrecompiled->call(tempExecutive, bytesConstRef(&in), "", "");
    out = callResult->execResult();
    codec->decode(&out, errorCode);
    BOOST_CHECK(errorCode == s256((int)CODE_INVALID_WEIGHT));

    // set an invalid weight(-1) to node, -1 will be overflow in u256
    in = codec->encodeWithSig("addSealer(string,uint256)", node1, s256(-1));
    callResult = consensusPrecompiled->call(tempExecutive, bytesConstRef(&in), "", "");
    out = callResult->execResult();
    codec->decode(&out, code);
    BOOST_CHECK(code == 0u);

    // set weight to node1 123
    in = codec->encodeWithSig("setWeight(string,uint256)", node1, u256(123));
    callResult = consensusPrecompiled->call(tempExecutive, bytesConstRef(&in), "", "");
    out = callResult->execResult();
    codec->decode(&out, code);
    BOOST_CHECK(code == 0u);

    // undefined method
    in = codec->encodeWithSig("null(string,uint256)", node1);
    callResult = consensusPrecompiled->call(tempExecutive, bytesConstRef(&in), "", "");
    out = callResult->execResult();
    codec->decode(&out, code);
    BOOST_CHECK(code == 0u);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test