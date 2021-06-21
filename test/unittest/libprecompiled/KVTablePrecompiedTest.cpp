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
 * @file KVTablePrecompiedTest.cpp
 * @author: kyonRay
 * @date 2021-06-19
 */

#include "PreCompiledFixture.h"
#include "libprecompiled/EntryPrecompiled.h"
#include "libprecompiled/KVTableFactoryPrecompiled.h"
#include "libprecompiled/KVTablePrecompiled.h"
#include "libprecompiled/extension/UserPrecompiled.h"
#include <bcos-framework/interfaces/storage/TableInterface.h>
#include <bcos-framework/testutils/TestPromptFixture.h>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;

namespace bcos::test
{
class KVTablePrecompiledFixture : public PrecompiledFixture
{
public:
    KVTablePrecompiledFixture()
    {
        kvTableFactoryPrecompiled = std::make_shared<KVTableFactoryPrecompiled>(hashImpl);
    }
    void initEvmEnv()
    {
        setIsWasm(false);
        kvTableFactoryPrecompiled->setMemoryTableFactory(context->getTableFactory());

        context->getTableFactory()->createTable("test", "id", "name,age");
        context->getTableFactory()->commit();

        auto table = context->getTableFactory()->openTable("test");
        BOOST_CHECK(table != nullptr);
        kvTablePrecompiled = std::make_shared<KVTablePrecompiled>(hashImpl);
        kvTablePrecompiled->setTable(table);
        address = Address(context->registerPrecompiled(kvTablePrecompiled));
    }

    void initWasmEnv()
    {
        setIsWasm(true);
        kvTableFactoryPrecompiled->setMemoryTableFactory(context->getTableFactory());

        context->getTableFactory()->createTable("/data/test", "id", "name,age");
        context->getTableFactory()->commit();

        auto table = context->getTableFactory()->openTable("/data/test");
        BOOST_CHECK(table != nullptr);
        kvTablePrecompiled = std::make_shared<KVTablePrecompiled>(hashImpl);
        kvTablePrecompiled->setTable(table);
        wasmAddress = context->registerPrecompiled(kvTablePrecompiled);
    }

    virtual ~KVTablePrecompiledFixture() {}

    Address address;
    std::string wasmAddress;
    KVTableFactoryPrecompiled::Ptr kvTableFactoryPrecompiled;
    KVTablePrecompiled::Ptr kvTablePrecompiled;
    int addressCount = 0x10000;
};

BOOST_FIXTURE_TEST_SUITE(precompiledKVTableTest, KVTablePrecompiledFixture)

BOOST_AUTO_TEST_CASE(hash)
{
    initEvmEnv();
    auto h = kvTablePrecompiled->hash();
    BOOST_CHECK(h == HashType());
}

BOOST_AUTO_TEST_CASE(toString)
{
    initEvmEnv();
    BOOST_CHECK_EQUAL(kvTablePrecompiled->toString(), "KVTable");
}

BOOST_AUTO_TEST_CASE(get_set_evm)
{
    initEvmEnv();

    // get not exist key
    bytes in = codec->encodeWithSig("get(string)", std::string("kk"));
    auto callResult = kvTablePrecompiled->call(context, bytesConstRef(&in), "", "", gas);
    bytes out = callResult->execResult();
    bool status = true;
    Address entryAddress;
    codec->decode(bytesConstRef(&out), status, entryAddress);
    BOOST_TEST(status == false);

    // set a entry
    auto table = kvTablePrecompiled->getTable();
    auto newEntry = table->newEntry();
    newEntry->setField("name", "kk");
    newEntry->setField("age", "100");
    auto entryPrecompiled = std::make_shared<EntryPrecompiled>(hashImpl);
    entryPrecompiled->setEntry(newEntry);
    auto entryAddress1 = context->registerPrecompiled(entryPrecompiled);
    BOOST_CHECK(Address(entryAddress1) == Address(std::to_string(addressCount + 2)));

    // call set
    auto addr = Address(entryAddress1, FixedBytes<20>::FromBinary);
    bytes in2 = codec->encodeWithSig("set(string,address)", std::string("123"), addr);
    callResult = kvTablePrecompiled->call(context, bytesConstRef(&in2), "0x00001", "", gas);
    bytes out1 = callResult->execResult();
    u256 num;
    codec->decode(bytesConstRef(&out1), num);
    BOOST_TEST(num == 1);

    // call get
    in = codec->encodeWithSig("get(string)", std::string("123"));
    callResult = kvTablePrecompiled->call(context, bytesConstRef(&in), "", "", gas);
    out = callResult->execResult();
    codec->decode(bytesConstRef(&out), status, entryAddress);
    BOOST_TEST(status == true);

    // get field in entry
    auto entryPrecompiled2 =
        std::dynamic_pointer_cast<EntryPrecompiled>(context->getPrecompiled(entryAddress1));
    auto entry = entryPrecompiled2->getEntry();
    BOOST_TEST(entry->getField("name") == "kk");
}

BOOST_AUTO_TEST_CASE(get_set_wasm)
{
    initWasmEnv();

    // get not exist key
    bytes in = codec->encodeWithSig("get(string)", std::string("kk"));
    auto callResult = kvTablePrecompiled->call(context, bytesConstRef(&in), "", "", gas);
    bytes out = callResult->execResult();
    bool status = true;
    std::string entryAddress;
    codec->decode(bytesConstRef(&out), status, entryAddress);
    BOOST_TEST(status == false);

    // set a entry
    auto table = kvTablePrecompiled->getTable();
    auto newEntry = table->newEntry();
    newEntry->setField("name", "kk");
    newEntry->setField("age", "100");
    auto entryPrecompiled = std::make_shared<EntryPrecompiled>(hashImpl);
    entryPrecompiled->setEntry(newEntry);
    auto entryAddress1 = context->registerPrecompiled(entryPrecompiled);
    BOOST_CHECK(Address(entryAddress1) == Address(std::to_string(addressCount + 2)));

    // call set
    auto addr = Address(entryAddress1, FixedBytes<20>::FromBinary);
    bytes in2 = codec->encodeWithSig("set(string,string)", std::string("123"), addr);
    callResult = kvTablePrecompiled->call(context, bytesConstRef(&in2), "0x00001", "", gas);
    bytes out1 = callResult->execResult();
    u256 num;
    codec->decode(bytesConstRef(&out1), num);
    BOOST_TEST(num == 1);

    // call get
    in = codec->encodeWithSig("get(string)", std::string("123"));
    callResult = kvTablePrecompiled->call(context, bytesConstRef(&in), "", "", gas);
    out = callResult->execResult();
    codec->decode(bytesConstRef(&out), status, entryAddress);
    BOOST_TEST(status == true);

    // get field in entry
    auto entryPrecompiled2 =
        std::dynamic_pointer_cast<EntryPrecompiled>(context->getPrecompiled(entryAddress1));
    auto entry = entryPrecompiled2->getEntry();
    BOOST_TEST(entry->getField("name") == "kk");
}

BOOST_AUTO_TEST_CASE(newEntryTest)
{
    {
        initEvmEnv();
        bytes in = codec->encodeWithSig("newEntry()");
        auto callResult = kvTablePrecompiled->call(context, bytesConstRef(&in), "", "", gas);
        bytes out1 = callResult->execResult();
        Address address;
        codec->decode(&out1, address);
        std::string s((char*)address.data(), 20);
        BOOST_CHECK(s == "00000000000000065538");
    }
    {
        initWasmEnv();
        bytes in = codec->encodeWithSig("newEntry()");
        auto callResult = kvTablePrecompiled->call(context, bytesConstRef(&in), "", "", gas);
        bytes out1 = callResult->execResult();
        std::string address;
        codec->decode(&out1, address);
        BOOST_CHECK(Address(address) == Address(std::to_string(addressCount + 2)));
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test