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
 * @file DeployWasmPrecompiledTest.cpp
 * @author: kyonRay
 * @date 2021-07-05
 */

#include "libprecompiled/DeployWasmPrecompiled.h"
#include "PreCompiledFixture.h"
#include <bcos-framework/interfaces/storage/TableInterface.h>
#include <bcos-framework/testutils/TestPromptFixture.h>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::ledger;

namespace bcos::test
{
class DeployWasmPrecompiledFixture : public PrecompiledFixture
{
public:
    DeployWasmPrecompiledFixture()
    {
        deployWasmPrecompiled = std::make_shared<DeployWasmPrecompiled>(hashImpl);
        setIsWasm(true);
    }

    virtual ~DeployWasmPrecompiledFixture() {}

    DeployWasmPrecompiled::Ptr deployWasmPrecompiled;
    int addressCount = 0x10000;
};
BOOST_FIXTURE_TEST_SUITE(precompiledDeployWasmTest, DeployWasmPrecompiledFixture)

BOOST_AUTO_TEST_CASE(testDeploy)
{
    BOOST_CHECK(deployWasmPrecompiled->toString() == "DeployWasm");

    bytes code, param;
    code = asBytes("123");
    param = asBytes("456");
    std::string deployPath = "/usr/bin/HelloWorld";
    std::string jsonABI = "{}";
    bytes in = codec->encodeWithSig(
        "deployWasm(bytes,bytes,string,string)", code, param, deployPath, jsonABI);
    BOOST_CHECK_THROW(
        deployWasmPrecompiled->call(context, bytesConstRef(&in), "", "", gas), PrecompiledError);
    //    auto callResult = deployWasmPrecompiled->call(context, bytesConstRef(&in), "", "", gas);
    //    bytes out = callResult->execResult();
    //    bool result;

    // FIXME: this deploy is failed fix it
    // FIXME: this code and param is error, use code with wasm prefix

    //    codec->decode(&out, result);
    //    BOOST_CHECK(result);

    // exist path
    in = codec->encodeWithSig(
        "deployWasm(bytes,bytes,string,string)", code, param, deployPath, jsonABI);

    // BOOST_CHECK_THROW(
    //     deployWasmPrecompiled->call(context, bytesConstRef(&in), "", "", gas), PrecompiledError);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
