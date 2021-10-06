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
 * @file TablePrecompiledTest.cpp
 * @author: kyonRay
 * @date 2021-06-21
 */

#include "precompiled/TablePrecompiled.h"
#include "PreCompiledFixture.h"
#include "precompiled/ConditionPrecompiled.h"
#include "precompiled/EntriesPrecompiled.h"
#include "precompiled/EntryPrecompiled.h"
#include "precompiled/TableFactoryPrecompiled.h"
#include <bcos-framework/testutils/TestPromptFixture.h>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;

namespace bcos::test
{
class TablePrecompiledFixture : public PrecompiledFixture
{
public:
    TablePrecompiledFixture()
    {
        tableFactoryPrecompiled = std::make_shared<TableFactoryPrecompiled>(hashImpl);
    }
    void initEvmEnv()
    {
        setIsWasm(false);
        tableFactoryPrecompiled->setMemoryTableFactory(context->storage());

        context->storage()->createTable("test", "name,age");
        auto sysTable = context->storage()->openTable(StorageInterface::SYS_TABLES);
        auto sysEntry = sysTable->getRow("test");
        sysEntry->setField(StorageInterface::SYS_TABLE_VALUE_FIELDS, "name,age,id");
        sysTable->setRow("test", std::move(sysEntry.value()));

        auto table = context->storage()->openTable("test");
        BOOST_CHECK(table != std::nullopt);
        tablePrecompiled = std::make_shared<TablePrecompiled>(hashImpl);
        tablePrecompiled->setTable(std::make_shared<Table>(table.value()));
        tablePrecompiled->setKeyField(key);
    }

    void initWasmEnv()
    {
        setIsWasm(true);
        tableFactoryPrecompiled->setMemoryTableFactory(context->storage());

        context->storage()->createTable("/tables/test", "name,age");
        auto sysTable = context->storage()->openTable(StorageInterface::SYS_TABLES);
        auto sysEntry = sysTable->getRow("/tables/test");
        sysEntry->setField(StorageInterface::SYS_TABLE_VALUE_FIELDS, "name,age,id");
        sysTable->setRow("/tables/test", std::move(sysEntry.value()));

        auto table = context->storage()->openTable("/tables/test");
        BOOST_CHECK(table != std::nullopt);
        tablePrecompiled = std::make_shared<TablePrecompiled>(hashImpl);
        tablePrecompiled->setTable(std::make_shared<Table>(table.value()));
        tablePrecompiled->setKeyField(key);
    }

    void initTableData()
    {
        auto table = tablePrecompiled->getTable();
        auto entry1 = table->newEntry();
        entry1.setField("name", "kk");
        entry1.setField("age", "20");
        entry1.setField("id", "1");
        table->setRow("1", entry1);

        auto entry2 = table->newEntry();
        entry2.setField("name", "ll");
        entry2.setField("age", "18");
        entry2.setField("id", "2");
        table->setRow("2", entry2);
    }

    virtual ~TablePrecompiledFixture() {}

    TableFactoryPrecompiled::Ptr tableFactoryPrecompiled;
    TablePrecompiled::Ptr tablePrecompiled;
    std::string tableName = "test";
    std::string key = "id";
    std::string value = "name,age";
    int addressCount = 0x10000;
};
BOOST_FIXTURE_TEST_SUITE(precompiledTableTest, TablePrecompiledFixture)

BOOST_AUTO_TEST_CASE(toString)
{
    initEvmEnv();
    BOOST_CHECK_EQUAL(tablePrecompiled->toString(), "Table");
}

BOOST_AUTO_TEST_CASE(newEntryTest)
{
    {
        initEvmEnv();
        bytes in = codec->encodeWithSig("newEntry()");
        auto callResult = tablePrecompiled->call(context, bytesConstRef(&in), "", "");
        bytes out1 = callResult->execResult();
        Address address;
        codec->decode(&out1, address);
        BOOST_CHECK(address.hex() == "0000000000000000000000000000000000010001");
    }
    {
        initWasmEnv();
        bytes in = codec->encodeWithSig("newEntry()");
        auto callResult = tablePrecompiled->call(context, bytesConstRef(&in), "", "");
        bytes out1 = callResult->execResult();
        std::string address;
        codec->decode(&out1, address);
        BOOST_CHECK(Address(address) == Address(addressCount + 1));
    }
}

BOOST_AUTO_TEST_CASE(newConditionTest)
{
    {
        initEvmEnv();
        bytes in = codec->encodeWithSig("newCondition()");
        auto callResult = tablePrecompiled->call(context, bytesConstRef(&in), "", "");
        bytes out1 = callResult->execResult();
        Address address;
        codec->decode(&out1, address);
        BOOST_CHECK(address.hex() == "0000000000000000000000000000000000010001");
    }
    {
        initWasmEnv();
        bytes in = codec->encodeWithSig("newCondition()");
        auto callResult = tablePrecompiled->call(context, bytesConstRef(&in), "", "");
        bytes out1 = callResult->execResult();
        std::string address;
        codec->decode(&out1, address);
        BOOST_CHECK(Address(address) == Address(addressCount + 1));
    }
}

BOOST_AUTO_TEST_CASE(select_evm)
{
    initEvmEnv();
    initTableData();
    /*
     * id | name | age
     * 1    kk      20
     * 2    ll      18
     */

    // select 1 row
    {
        auto condition = std::make_shared<precompiled::Condition>();
        condition->EQ("id", "1");
        condition->EQ("name", "kk");
        auto conditionPrecompiled = std::make_shared<ConditionPrecompiled>(hashImpl);
        conditionPrecompiled->setCondition(condition);

        Address conditionAddress = Address(context->registerPrecompiled(conditionPrecompiled));

        bytes in = codec->encodeWithSig("select(address)", conditionAddress);
        auto callResult = tablePrecompiled->call(context, bytesConstRef(&in), "", "");
        bytes out = callResult->execResult();
        Address entriesAddress;
        codec->decode(bytesConstRef(&out), entriesAddress);
        auto entriesPrecompiled = std::dynamic_pointer_cast<EntriesPrecompiled>(
            context->getPrecompiled(entriesAddress.hex()));

        EntriesPtr entries = entriesPrecompiled->getEntriesPtr();
        BOOST_TEST(entries->size() == 1u);
    }

    // select 0 row
    {
        auto condition = std::make_shared<precompiled::Condition>();
        condition->GE("id", "2");
        condition->EQ("name", "kk");
        auto conditionPrecompiled = std::make_shared<ConditionPrecompiled>(hashImpl);
        conditionPrecompiled->setCondition(condition);

        Address conditionAddress = Address(context->registerPrecompiled(conditionPrecompiled));

        bytes in = codec->encodeWithSig("select(address)", conditionAddress);
        auto callResult = tablePrecompiled->call(context, bytesConstRef(&in), "", "");
        bytes out = callResult->execResult();
        Address entriesAddress;
        codec->decode(bytesConstRef(&out), entriesAddress);
        auto entriesPrecompiled = std::dynamic_pointer_cast<EntriesPrecompiled>(
            context->getPrecompiled(entriesAddress.hex()));

        EntriesPtr entries = entriesPrecompiled->getEntriesPtr();
        BOOST_TEST(entries->size() == 0u);
    }

    // select key not exist in condition
    {
        auto condition = std::make_shared<precompiled::Condition>();
        condition->EQ("name", "kk");
        auto conditionPrecompiled = std::make_shared<ConditionPrecompiled>(hashImpl);
        conditionPrecompiled->setCondition(condition);

        Address conditionAddress = Address(context->registerPrecompiled(conditionPrecompiled));

        bytes in = codec->encodeWithSig("select(address)", conditionAddress);
        auto callResult = tablePrecompiled->call(context, bytesConstRef(&in), "", "");
        bytes out = callResult->execResult();
        Address entriesAddress;
        codec->decode(bytesConstRef(&out), entriesAddress);
        auto entriesPrecompiled = std::dynamic_pointer_cast<EntriesPrecompiled>(
            context->getPrecompiled(entriesAddress.hex()));

        EntriesPtr entries = entriesPrecompiled->getEntriesPtr();
        BOOST_TEST(entries->size() == 0u);
    }

    // select 2 rows
    {
        auto condition = std::make_shared<precompiled::Condition>();
        condition->LE("id", "2");
        auto conditionPrecompiled = std::make_shared<ConditionPrecompiled>(hashImpl);
        conditionPrecompiled->setCondition(condition);

        Address conditionAddress = Address(context->registerPrecompiled(conditionPrecompiled));

        bytes in = codec->encodeWithSig("select(address)", conditionAddress);
        auto callResult = tablePrecompiled->call(context, bytesConstRef(&in), "", "");
        bytes out = callResult->execResult();
        Address entriesAddress;
        codec->decode(bytesConstRef(&out), entriesAddress);
        auto entriesPrecompiled = std::dynamic_pointer_cast<EntriesPrecompiled>(
            context->getPrecompiled(entriesAddress.hex()));
        EntriesPtr entries = entriesPrecompiled->getEntriesPtr();
        BOOST_TEST(entries->size() == 2u);

        BOOST_CHECK(entriesPrecompiled->toString() == "Entries");

        bytes in2 = codec->encodeWithSig("size()");
        callResult = entriesPrecompiled->call(context, bytesConstRef(&in2), "", "");
        out = callResult->execResult();
        u256 c;
        codec->decode(bytesConstRef(&out), c);
        BOOST_CHECK(c == 2u);

        bytes in3 = codec->encodeWithSig("get(int256)", u256(1));
        callResult = entriesPrecompiled->call(context, bytesConstRef(&in3), "", "");
        out = callResult->execResult();
        Address entryAddress;
        codec->decode(bytesConstRef(&out), entryAddress);
        BOOST_CHECK(entryAddress != Address());

        bytes in4 = codec->encodeWithSig("gets(int256)", u256(1));
        callResult = entriesPrecompiled->call(context, bytesConstRef(&in4), "", "");
        out = callResult->execResult();
    }
}

BOOST_AUTO_TEST_CASE(select_wasm)
{
    initWasmEnv();
    initTableData();
    /*
     * id | name | age
     * 1    kk      20
     * 2    ll      18
     */

    // select 1 row
    {
        auto condition = std::make_shared<precompiled::Condition>();
        condition->EQ("id", "1");
        condition->EQ("name", "kk");
        auto conditionPrecompiled = std::make_shared<ConditionPrecompiled>(hashImpl);
        conditionPrecompiled->setCondition(condition);

        std::string conditionAddress = context->registerPrecompiled(conditionPrecompiled);

        bytes in = codec->encodeWithSig("select(string)", conditionAddress);
        auto callResult = tablePrecompiled->call(context, bytesConstRef(&in), "", "");
        bytes out = callResult->execResult();
        std::string entriesAddress;
        codec->decode(bytesConstRef(&out), entriesAddress);
        auto entriesPrecompiled =
            std::dynamic_pointer_cast<EntriesPrecompiled>(context->getPrecompiled(entriesAddress));

        EntriesPtr entries = entriesPrecompiled->getEntriesPtr();
        BOOST_TEST(entries->size() == 1u);

        bytes in3 = codec->encodeWithSig("get(int256)", u256(0));
        callResult = entriesPrecompiled->call(context, bytesConstRef(&in3), "", "");
        out = callResult->execResult();
        std::string entryAddress;
        codec->decode(bytesConstRef(&out), entryAddress);
        BOOST_CHECK(!entryAddress.empty());
    }

    // select 0 row
    {
        auto condition = std::make_shared<precompiled::Condition>();
        condition->GE("id", "2");
        condition->EQ("name", "kk");
        auto conditionPrecompiled = std::make_shared<ConditionPrecompiled>(hashImpl);
        conditionPrecompiled->setCondition(condition);

        std::string conditionAddress = context->registerPrecompiled(conditionPrecompiled);

        bytes in = codec->encodeWithSig("select(string)", conditionAddress);
        auto callResult = tablePrecompiled->call(context, bytesConstRef(&in), "", "");
        bytes out = callResult->execResult();
        std::string entriesAddress;
        codec->decode(bytesConstRef(&out), entriesAddress);
        auto entriesPrecompiled =
            std::dynamic_pointer_cast<EntriesPrecompiled>(context->getPrecompiled(entriesAddress));

        EntriesPtr entries = entriesPrecompiled->getEntriesPtr();
        BOOST_TEST(entries->size() == 0u);
    }

    // select key not exist in condition
    {
        auto condition = std::make_shared<precompiled::Condition>();
        condition->EQ("name", "kk");
        auto conditionPrecompiled = std::make_shared<ConditionPrecompiled>(hashImpl);
        conditionPrecompiled->setCondition(condition);

        std::string conditionAddress = context->registerPrecompiled(conditionPrecompiled);

        bytes in = codec->encodeWithSig("select(string)", conditionAddress);
        auto callResult = tablePrecompiled->call(context, bytesConstRef(&in), "", "");
        bytes out = callResult->execResult();
        std::string entriesAddress;
        codec->decode(bytesConstRef(&out), entriesAddress);
        auto entriesPrecompiled =
            std::dynamic_pointer_cast<EntriesPrecompiled>(context->getPrecompiled(entriesAddress));

        EntriesPtr entries = entriesPrecompiled->getEntriesPtr();
        BOOST_TEST(entries->size() == 0u);
    }
}

BOOST_AUTO_TEST_CASE(insert_evm)
{
    initEvmEnv();
    initTableData();

    // insert not exist row
    {
        auto entry = tablePrecompiled->getTable()->newEntry();
        entry.setField("id", "3");
        entry.setField("name", "bcos");
        auto entryPrecompiled = std::make_shared<EntryPrecompiled>(hashImpl);
        entryPrecompiled->setEntry(std::make_shared<storage::Entry>(entry));
        auto entryAddress = Address(context->registerPrecompiled(entryPrecompiled));

        bytes in = codec->encodeWithSig("insert(address)", entryAddress);
        auto callResult = tablePrecompiled->call(context, bytesConstRef(&in), "", "");
        bytes out = callResult->execResult();
        s256 num;
        codec->decode(bytesConstRef(&out), num);
        BOOST_TEST(num == 1u);
    }

    // insert key exist row
    {
        auto entry = tablePrecompiled->getTable()->newEntry();
        entry.setField("id", "2");
        entry.setField("name", "bcos");
        auto entryPrecompiled = std::make_shared<EntryPrecompiled>(hashImpl);
        entryPrecompiled->setEntry(std::make_shared<storage::Entry>(entry));
        auto entryAddress = Address(context->registerPrecompiled(entryPrecompiled));

        bytes in = codec->encodeWithSig("insert(address)", entryAddress);
        auto callResult = tablePrecompiled->call(context, bytesConstRef(&in), "", "");
        bytes out = callResult->execResult();
        s256 num;
        codec->decode(bytesConstRef(&out), num);
        BOOST_TEST(num == s256((int)CODE_INSERT_KEY_EXIST));
    }

    // insert no key entry
    {
        auto entry = tablePrecompiled->getTable()->newEntry();
        entry.setField("name", "bcos");
        auto entryPrecompiled = std::make_shared<EntryPrecompiled>(hashImpl);
        entryPrecompiled->setEntry(std::make_shared<storage::Entry>(entry));
        auto entryAddress = Address(context->registerPrecompiled(entryPrecompiled));

        bytes in = codec->encodeWithSig("insert(address)", entryAddress);
        auto callResult = tablePrecompiled->call(context, bytesConstRef(&in), "", "");
        bytes out = callResult->execResult();
        s256 num;
        codec->decode(bytesConstRef(&out), num);
        BOOST_TEST(num == s256((int)CODE_KEY_NOT_EXIST_IN_ENTRY));
    }
}

BOOST_AUTO_TEST_CASE(insert_wasm)
{
    initWasmEnv();
    initTableData();

    // insert not exist row
    {
        auto entry = tablePrecompiled->getTable()->newEntry();
        entry.setField("id", "3");
        entry.setField("name", "bcos");
        auto entryPrecompiled = std::make_shared<EntryPrecompiled>(hashImpl);
        entryPrecompiled->setEntry(std::make_shared<storage::Entry>(entry));
        auto entryAddress = context->registerPrecompiled(entryPrecompiled);

        bytes in = codec->encodeWithSig("insert(string)", entryAddress);
        auto callResult = tablePrecompiled->call(context, bytesConstRef(&in), "", "");
        bytes out = callResult->execResult();
        u256 num;
        codec->decode(bytesConstRef(&out), num);
        BOOST_TEST(num == 1u);
    }
}

BOOST_AUTO_TEST_CASE(remove_evm)
{
    initEvmEnv();

    // remove 2 rows
    {
        initTableData();
        auto condition = std::make_shared<precompiled::Condition>();
        condition->GE("id", "1");
        auto conditionPrecompiled = std::make_shared<ConditionPrecompiled>(hashImpl);
        conditionPrecompiled->setCondition(condition);
        Address conditionAddress = Address(context->registerPrecompiled(conditionPrecompiled));
        bytes in = codec->encodeWithSig("remove(address)", conditionAddress);
        auto callResult = tablePrecompiled->call(context, bytesConstRef(&in), "", "");
        bytes out = callResult->execResult();
        u256 num;
        codec->decode(bytesConstRef(&out), num);
        BOOST_TEST(num == 1u);

        auto table = tablePrecompiled->getTable();
        auto entry = table->getRow("2");

        BOOST_TEST((!entry.has_value() || entry->status() == storage::Entry::DELETED));
    }

    // remove no row
    {
        initTableData();
        auto condition = std::make_shared<precompiled::Condition>();
        condition->GE("id", "3");
        auto conditionPrecompiled = std::make_shared<ConditionPrecompiled>(hashImpl);
        conditionPrecompiled->setCondition(condition);
        Address conditionAddress = Address(context->registerPrecompiled(conditionPrecompiled));
        bytes in = codec->encodeWithSig("remove(address)", conditionAddress);
        auto callResult = tablePrecompiled->call(context, bytesConstRef(&in), "", "");
        bytes out = callResult->execResult();
        u256 num;
        codec->decode(bytesConstRef(&out), num);
        BOOST_TEST(num == 1u);
    }

    // remove row without key
    {
        auto condition = std::make_shared<precompiled::Condition>();
        condition->GE("name", "kk");
        auto conditionPrecompiled = std::make_shared<ConditionPrecompiled>(hashImpl);
        conditionPrecompiled->setCondition(condition);
        Address conditionAddress = Address(context->registerPrecompiled(conditionPrecompiled));
        bytes in = codec->encodeWithSig("remove(address)", conditionAddress);
        auto callResult = tablePrecompiled->call(context, bytesConstRef(&in), "", "");
        bytes out = callResult->execResult();
        s256 num;
        codec->decode(bytesConstRef(&out), num);
        BOOST_TEST(num == s256((int)CODE_KEY_NOT_EXIST_IN_COND));
    }
}

BOOST_AUTO_TEST_CASE(remove_wasm)
{
    initWasmEnv();

    // remove 2 rows
    {
        initTableData();
        auto condition = std::make_shared<precompiled::Condition>();
        condition->GE("id", "1");
        auto conditionPrecompiled = std::make_shared<ConditionPrecompiled>(hashImpl);
        conditionPrecompiled->setCondition(condition);
        std::string conditionAddress = context->registerPrecompiled(conditionPrecompiled);
        bytes in = codec->encodeWithSig("remove(string)", conditionAddress);
        auto callResult = tablePrecompiled->call(context, bytesConstRef(&in), "", "");
        bytes out = callResult->execResult();
        u256 num;
        codec->decode(bytesConstRef(&out), num);
        BOOST_TEST(num == 1u);

        auto table = tablePrecompiled->getTable();
        auto entry = table->getRow("2");
        BOOST_TEST((!entry.has_value() || entry->status() == storage::Entry::DELETED));
    }
}

BOOST_AUTO_TEST_CASE(update_evm)
{
    initEvmEnv();
    initTableData();

    // update 2 rows
    {
        auto condition = std::make_shared<precompiled::Condition>();
        condition->GE("id", "1");
        auto conditionPrecompiled = std::make_shared<ConditionPrecompiled>(hashImpl);
        conditionPrecompiled->setCondition(condition);
        Address conditionAddress = Address(context->registerPrecompiled(conditionPrecompiled));

        auto entry = tablePrecompiled->getTable()->newEntry();
        entry.setField("age", "100");
        auto entryPrecompiled = std::make_shared<EntryPrecompiled>(hashImpl);
        entryPrecompiled->setEntry(std::make_shared<storage::Entry>(entry));
        auto entryAddress = Address(context->registerPrecompiled(entryPrecompiled));

        bytes in = codec->encodeWithSig("update(address,address)", entryAddress, conditionAddress);
        auto callResult = tablePrecompiled->call(context, bytesConstRef(&in), "", "");
        bytes out = callResult->execResult();
        u256 num;
        codec->decode(bytesConstRef(&out), num);
        BOOST_TEST(num == 2u);

        auto table = tablePrecompiled->getTable();
        BOOST_CHECK(table->getRow("1")->getField("age") == "100");
        BOOST_CHECK(table->getRow("2")->getField("age") == "100");
    }

    // update condition without key
    {
        auto condition = std::make_shared<precompiled::Condition>();
        condition->EQ("name", "ll");
        auto conditionPrecompiled = std::make_shared<ConditionPrecompiled>(hashImpl);
        conditionPrecompiled->setCondition(condition);
        Address conditionAddress = Address(context->registerPrecompiled(conditionPrecompiled));

        auto entry = tablePrecompiled->getTable()->newEntry();
        entry.setField("age", "100");
        auto entryPrecompiled = std::make_shared<EntryPrecompiled>(hashImpl);
        entryPrecompiled->setEntry(std::make_shared<storage::Entry>(entry));
        Address entryAddress = Address(context->registerPrecompiled(entryPrecompiled));

        bytes in = codec->encodeWithSig("update(address,address)", entryAddress, conditionAddress);
        auto callResult = tablePrecompiled->call(context, bytesConstRef(&in), "", "");
        bytes out = callResult->execResult();
        s256 num;
        codec->decode(bytesConstRef(&out), num);
        BOOST_TEST(num == s256((int)CODE_KEY_NOT_EXIST_IN_COND));
    }

    // update condition key not exist
    {
        auto condition = std::make_shared<precompiled::Condition>();
        condition->EQ("id", "3");
        auto conditionPrecompiled = std::make_shared<ConditionPrecompiled>(hashImpl);
        conditionPrecompiled->setCondition(condition);
        Address conditionAddress = Address(context->registerPrecompiled(conditionPrecompiled));

        auto entry = tablePrecompiled->getTable()->newEntry();
        entry.setField("age", "100");
        auto entryPrecompiled = std::make_shared<EntryPrecompiled>(hashImpl);
        entryPrecompiled->setEntry(std::make_shared<storage::Entry>(entry));
        Address entryAddress = Address(context->registerPrecompiled(entryPrecompiled));

        bytes in = codec->encodeWithSig("update(address,address)", entryAddress, conditionAddress);
        auto callResult = tablePrecompiled->call(context, bytesConstRef(&in), "", "");
        bytes out = callResult->execResult();
        s256 num;
        codec->decode(bytesConstRef(&out), num);
        BOOST_TEST(num == s256((int)CODE_UPDATE_KEY_NOT_EXIST));
    }
}

BOOST_AUTO_TEST_CASE(update_wasm)
{
    initWasmEnv();
    initTableData();

    // update 2 rows
    {
        auto condition = std::make_shared<precompiled::Condition>();
        condition->GE("id", "1");
        auto conditionPrecompiled = std::make_shared<ConditionPrecompiled>(hashImpl);
        conditionPrecompiled->setCondition(condition);
        auto conditionAddress = context->registerPrecompiled(conditionPrecompiled);

        auto entry = tablePrecompiled->getTable()->newEntry();
        entry.setField("age", "100");
        auto entryPrecompiled = std::make_shared<EntryPrecompiled>(hashImpl);
        entryPrecompiled->setEntry(std::make_shared<storage::Entry>(entry));
        auto entryAddress = context->registerPrecompiled(entryPrecompiled);

        bytes in = codec->encodeWithSig("update(string,string)", entryAddress, conditionAddress);
        auto callResult = tablePrecompiled->call(context, bytesConstRef(&in), "", "");
        bytes out = callResult->execResult();
        u256 num;
        codec->decode(bytesConstRef(&out), num);
        BOOST_TEST(num == 2u);

        auto table = tablePrecompiled->getTable();
        BOOST_CHECK(table->getRow("1")->getField("age") == "100");
        BOOST_CHECK(table->getRow("2")->getField("age") == "100");
    }
}

BOOST_AUTO_TEST_CASE(entry_test)
{
    initEvmEnv();
    context->storage()->createTable("testEntry", "int,uint,address,string,bytes64,bytes32");
    auto sysTable = context->storage()->openTable(StorageInterface::SYS_TABLES);
    auto sysEntry = sysTable->getRow("testEntry");
    sysEntry->setField(
        StorageInterface::SYS_TABLE_VALUE_FIELDS, "int,uint,address,string,bytes64,bytes32,key");
    sysTable->setRow("testEntry", std::move(sysEntry.value()));
    auto table = context->storage()->openTable("testEntry");
    auto entry = table->newEntry();

    auto entryPrecompiled = std::make_shared<precompiled::EntryPrecompiled>(hashImpl);
    entryPrecompiled->setEntry(std::make_shared<storage::Entry>(entry));

    BOOST_CHECK(entryPrecompiled->toString() == "Entry");

    bytes in = codec->encodeWithSig("set(string,int256)", std::string("int"), s256(-1));
    entryPrecompiled->call(context, bytesConstRef(&in), "", "");

    in = codec->encodeWithSig("set(string,uint256)", std::string("uint"), u256(1));
    entryPrecompiled->call(context, bytesConstRef(&in), "", "");

    in = codec->encodeWithSig("set(string,address)", std::string("address"), Address(123));
    entryPrecompiled->call(context, bytesConstRef(&in), "", "");

    in = codec->encodeWithSig("set(string,string)", std::string("string"), std::string("string"));
    entryPrecompiled->call(context, bytesConstRef(&in), "", "");

    // cover undefined method
    in = codec->encodeWithSig("null(string,string)");
    entryPrecompiled->call(context, bytesConstRef(&in), "", "");

    std::string bytes64;
    for (int i = 0; i < 64; ++i)
    {
        bytes64 += "1";
    }
    in = codec->encodeWithSig("set(string,string)", std::string("bytes64"), bytes64);
    entryPrecompiled->call(context, bytesConstRef(&in), "", "");

    std::string bytes32;
    for (int i = 0; i < 32; ++i)
    {
        bytes32 += "1";
    }
    in = codec->encodeWithSig("set(string,string)", std::string("bytes32"), bytes32);
    entryPrecompiled->call(context, bytesConstRef(&in), "", "");

    in = codec->encodeWithSig("set(string,string)", std::string("key"), std::string("keyValue"));
    entryPrecompiled->call(context, bytesConstRef(&in), "", "");

    in = codec->encodeWithSig("getInt(string)", std::string("int"));
    auto callResult = entryPrecompiled->call(context, bytesConstRef(&in), "", "");
    bytes out = callResult->execResult();
    s256 s;
    codec->decode(&out, s);
    BOOST_CHECK(s == s256(-1));

    in = codec->encodeWithSig("getUInt(string)", std::string("uint"));
    callResult = entryPrecompiled->call(context, bytesConstRef(&in), "", "");
    out = callResult->execResult();
    u256 u;
    codec->decode(&out, u);
    BOOST_CHECK(u == u256(1));

    in = codec->encodeWithSig("getAddress(string)", std::string("address"));
    callResult = entryPrecompiled->call(context, bytesConstRef(&in), "", "");
    out = callResult->execResult();
    Address a;
    codec->decode(&out, a);
    BOOST_CHECK(a == Address(123));

    in = codec->encodeWithSig("getString(string)", std::string("string"));
    callResult = entryPrecompiled->call(context, bytesConstRef(&in), "", "");
    out = callResult->execResult();
    std::string as;
    codec->decode(&out, as);
    BOOST_CHECK(as == "string");

    in = codec->encodeWithSig("getBytes64(string)", std::string("bytes64"));
    callResult = entryPrecompiled->call(context, bytesConstRef(&in), "", "");
    out = callResult->execResult();
    bcos::string32 a32;
    bcos::string32 b32;
    codec->decode(&out, a32, b32);
    BOOST_CHECK(a32 == bcos::codec::toString32(bytes32));
    BOOST_CHECK(b32 == bcos::codec::toString32(bytes32));

    in = codec->encodeWithSig("getBytes32(string)", std::string("bytes32"));
    callResult = entryPrecompiled->call(context, bytesConstRef(&in), "", "");
    out = callResult->execResult();
    codec->decode(&out, a32);
    BOOST_CHECK(a32 == bcos::codec::toString32(bytes32));
}

BOOST_AUTO_TEST_CASE(condition_test)
{
    initWasmEnv();
    auto conditionPrecompiled = std::make_shared<ConditionPrecompiled>(hashImpl);
    auto condition = std::make_shared<precompiled::Condition>();
    conditionPrecompiled->setCondition(condition);
    bytes eqInt = codec->encodeWithSig("EQ(string,int256)", std::string("test1"), s256(-1));
    bytes eqString =
        codec->encodeWithSig("EQ(string,string)", std::string("test1"), std::string("test1"));
    bytes eqAddress =
        codec->encodeWithSig("EQ(string,address)", std::string("test1"), Address(123));
    bytes ge = codec->encodeWithSig("GE(string,int256)", std::string("test1"), s256(-1));
    bytes gt = codec->encodeWithSig("GT(string,int256)", std::string("test1"), s256(-1));
    bytes le = codec->encodeWithSig("LE(string,int256)", std::string("test1"), s256(-1));
    bytes lt = codec->encodeWithSig("LT(string,int256)", std::string("test1"), s256(-1));
    bytes ne = codec->encodeWithSig("NE(string,int256)", std::string("test1"), s256(-1));
    bytes neString =
        codec->encodeWithSig("NE(string,string)", std::string("test1"), std::string("123"));
    bytes limit = codec->encodeWithSig("limit(int256)", s256(-1));
    bytes limit2 = codec->encodeWithSig("limit(int256,int256)", s256(-1), s256(1));
    conditionPrecompiled->call(context, &eqInt, "", "");
    conditionPrecompiled->call(context, &eqString, "", "");
    conditionPrecompiled->call(context, &eqAddress, "", "");
    conditionPrecompiled->call(context, &ge, "", "");
    conditionPrecompiled->call(context, &gt, "", "");
    conditionPrecompiled->call(context, &le, "", "");
    conditionPrecompiled->call(context, &lt, "", "");
    conditionPrecompiled->call(context, &ne, "", "");
    conditionPrecompiled->call(context, &neString, "", "");
    conditionPrecompiled->call(context, &limit, "", "");
    conditionPrecompiled->call(context, &limit2, "", "");
    BOOST_CHECK(conditionPrecompiled->toString() == "Condition");
    BOOST_CHECK(conditionPrecompiled->getCondition()->m_conditions.size() == 9);
}

BOOST_AUTO_TEST_CASE(undifined_method)
{
    initEvmEnv();
    initTableData();

    bytes in = codec->encodeWithSig("undefined(string)", std::string("1"));
    auto callResult = tablePrecompiled->call(context, bytesConstRef(&in), "", "");
    bytes out = callResult->execResult();
    u256 num;
    codec->decode(bytesConstRef(&out), num);
    BOOST_TEST(num == 0u);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test