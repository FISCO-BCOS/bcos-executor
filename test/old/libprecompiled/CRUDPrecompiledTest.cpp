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
 * @file CRUDPrecompiledTest.cpp
 * @author: kyonRay
 * @date 2021-08-30
 */

#include "libprecompiled/CRUDPrecompiled.h"
#include "PreCompiledFixture.h"
#include "libprecompiled/ConditionPrecompiled.h"
#include "libprecompiled/EntriesPrecompiled.h"
#include "libprecompiled/EntryPrecompiled.h"
#include "libprecompiled/TableFactoryPrecompiled.h"
#include "libprecompiled/extension/UserPrecompiled.h"
#include <bcos-framework/interfaces/storage/TableInterface.h>
#include <bcos-framework/testutils/TestPromptFixture.h>
#include <json/json.h>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace std;

namespace bcos::test
{
class CRUDPrecompiledTest : public PrecompiledFixture
{
public:
    CRUDPrecompiledTest()
    {
        tableFactoryPrecompiled = std::make_shared<TableFactoryPrecompiled>(hashImpl);
        crudPrecompiled = std::make_shared<CRUDPrecompiled>(hashImpl);
        setIsWasm(false);
        tableFactoryPrecompiled->setMemoryTableFactory(context->getTableFactory());
    }

    virtual ~CRUDPrecompiledTest() {}

    TableFactoryPrecompiled::Ptr tableFactoryPrecompiled;
    CRUDPrecompiled::Ptr crudPrecompiled;
};
BOOST_FIXTURE_TEST_SUITE(precompiledCRUDTest, CRUDPrecompiledTest)

BOOST_AUTO_TEST_CASE(CRUD_evm)
{
    // createTable
    std::string tableName = "t_test", tableName2 = "t_demo", key = "name",
        valueField = "item_id,item_name";
    bytes param = codec->encodeWithSig("createTable(string,string,string)", tableName, key, valueField);
    auto callResult = tableFactoryPrecompiled->call(context, bytesConstRef(&param),"","", gas);
    bytes out = callResult->execResult();
    s256 createResult = 0;
    codec->decode(&out, createResult);
    BOOST_TEST(createResult == 0u);

    // desc
    param.clear();
    out.clear();
    param = codec->encodeWithSig("desc(string)", tableName);
    callResult = crudPrecompiled->call(context, bytesConstRef(&param),"","",gas);
    out = callResult->execResult();
    string keyField, valueField2;
    codec->decode(&out, keyField, valueField2);
    BOOST_TEST(keyField == "name");
    BOOST_TEST(valueField == valueField2);

    // desc not exist
    param.clear();
    out.clear();
    param = codec->encodeWithSig("desc(string)", std::string("error"));
    callResult = crudPrecompiled->call(context, bytesConstRef(&param),"","",gas);
    out = callResult->execResult();
    codec->decode(&out, keyField, valueField);
    BOOST_TEST(keyField == "");
    BOOST_TEST(valueField == "");

    // insert
    std::string insertFunc = "insert(string,string,string)";
    std::string entryStr = "{\"item_id\":\"1\",\"name\":\"fruit\",\"item_name\":\"apple\"}";
    param.clear();
    out.clear();
    param = codec->encodeWithSig(insertFunc, tableName, entryStr, std::string(""));
    callResult = crudPrecompiled->call(context, bytesConstRef(&param), "","",gas);
    out = callResult->execResult();
    s256 insertResult = 0;
    codec->decode(&out, insertResult);
    BOOST_TEST(insertResult == 1u);

    // insert table not exist
    entryStr = "{\"item_id\":\"1\",\"name\":\"fruit\",\"item_name\":\"apple\"}";
    param.clear();
    out.clear();
    param = codec->encodeWithSig(insertFunc, tableName2, entryStr, std::string(""));
    callResult = crudPrecompiled->call(context, bytesConstRef(&param), "","",gas);
    out = callResult->execResult();
    insertResult = 0;
    codec->decode(&out, insertResult);
    BOOST_TEST(insertResult == s256((int)CODE_TABLE_NOT_EXIST));

    // insert entry error
    entryStr = "{\"item_id\"1\",\"name\":\"fruit\",\"item_name\":\"apple\"}";
    param.clear();
    out.clear();
    param = codec->encodeWithSig(insertFunc, tableName, entryStr, std::string(""));
    callResult = crudPrecompiled->call(context, bytesConstRef(&param), "","",gas);
    out = callResult->execResult();
    insertResult = 0;
    codec->decode(&out, insertResult);
    BOOST_TEST(insertResult == s256((int)CODE_PARSE_ENTRY_ERROR));

    // select
    std::string selectFunc = "select(string,string,string)";
    std::string conditionStr = "{\"name\":{\"eq\":\"fruit\"},\"item_id\":{\"eq\":\"1\"},\"limit\":{\"limit\":\"0,1\"}}";
    param.clear();
    out.clear();
    param = codec->encodeWithSig(selectFunc, tableName, conditionStr, std::string(""));
    callResult = crudPrecompiled->call(context, bytesConstRef(&param), "","",gas);
    out = callResult->execResult();
    std::string selectResult;
    codec->decode(&out, selectResult);
    Json::Value entryJson;
    Json::Reader reader;
    reader.parse(selectResult, entryJson);
    BOOST_TEST(entryJson.size() == 1);

    // select table not exist
    param.clear();
    out.clear();
    param = codec->encodeWithSig(selectFunc, tableName2, conditionStr, std::string(""));
    callResult = crudPrecompiled->call(context, bytesConstRef(&param), "","",gas);
    out = callResult->execResult();
    s256 selectResult2 = 0;
    codec->decode(&out, selectResult2);
    BOOST_TEST(selectResult2 == s256((int)CODE_TABLE_NOT_EXIST));

    // select condition error
    conditionStr = "{\"item_id\":\"eq\":\"1\"},\"limit\":{\"limit\":\"0,1\"}}";
    param.clear();
    out.clear();
    param = codec->encodeWithSig(selectFunc, tableName, conditionStr, std::string(""));
    callResult = crudPrecompiled->call(context, bytesConstRef(&param), "","",gas);
    out = callResult->execResult();
    selectResult2 = 0;
    codec->decode(&out, selectResult2);
    BOOST_TEST(selectResult2 == s256((int)CODE_PARSE_CONDITION_ERROR));

    // update
    std::string updateFunc = "update(string,string,string,string)";
    entryStr = "{\"item_id\":\"1\",\"item_name\":\"orange\"}";
    conditionStr = "{\"name\":{\"eq\":\"fruit\"}}";
    param.clear();
    out.clear();
    param = codec->encodeWithSig(updateFunc, tableName, entryStr, conditionStr, std::string(""));
    callResult = crudPrecompiled->call(context, bytesConstRef(&param), "","",gas);
    out = callResult->execResult();
    s256 updateResult = 0;
    codec->decode(&out, updateResult);
    BOOST_TEST(updateResult == 1u);

    // update table not exist
    entryStr = "{\"item_id\":\"1\",\"item_name\":\"orange\"}";
    conditionStr = "{\"name\":{\"eq\":\"fruit\"}}";
    param.clear();
    out.clear();
    param = codec->encodeWithSig(updateFunc, tableName2, entryStr, conditionStr, std::string(""));
    callResult = crudPrecompiled->call(context, bytesConstRef(&param), "","",gas);
    out = callResult->execResult();
    updateResult = 0;
    codec->decode(&out, updateResult);
    BOOST_TEST(updateResult == s256((int)CODE_TABLE_NOT_EXIST));

    // update entry error
    entryStr = "{\"item_id\"1\",\"item_name\":\"apple\"}";
    conditionStr = "{\"name\":{\"eq\":\"fruit\"}}";
    param.clear();
    out.clear();
    param = codec->encodeWithSig(updateFunc, tableName, entryStr, conditionStr, std::string(""));
    callResult = crudPrecompiled->call(context, bytesConstRef(&param), "","",gas);
    out = callResult->execResult();
    updateResult = 0;
    codec->decode(&out, updateResult);
    BOOST_TEST(updateResult == s256((int)CODE_PARSE_ENTRY_ERROR));

    // update condition error
    entryStr = "{\"item_id\":\"1\",\"item_name\":\"orange\"}";
    conditionStr = "{\"name\"\"eq\":\"fruit\"}}";
    param.clear();
    out.clear();
    param = codec->encodeWithSig(updateFunc, tableName, entryStr, conditionStr, std::string(""));
    callResult = crudPrecompiled->call(context, bytesConstRef(&param), "","",gas);
    out = callResult->execResult();
    updateResult = 0;
    codec->decode(&out, updateResult);
    BOOST_TEST(updateResult == s256((int)CODE_PARSE_CONDITION_ERROR));

    // remove
    std::string removeFunc = "remove(string,string,string)";
    conditionStr = "{\"name\":{\"eq\":\"fruit\"}}";
    param.clear();
    out.clear();
    param = codec->encodeWithSig(removeFunc, tableName, conditionStr, std::string(""));
    callResult = crudPrecompiled->call(context, bytesConstRef(&param), "","",gas);
    out = callResult->execResult();
    s256 removeResult = 0;
    codec->decode(&out, removeResult);
    BOOST_TEST(removeResult == 1u);

    // remove table not exist
    conditionStr = "{\"name\":{\"eq\":\"fruit\"}}";
    param.clear();
    out.clear();
    param = codec->encodeWithSig(removeFunc, tableName2, conditionStr, std::string(""));
    callResult = crudPrecompiled->call(context, bytesConstRef(&param), "","",gas);
    out = callResult->execResult();
    removeResult = 0;
    codec->decode(&out, removeResult);
    BOOST_TEST(removeResult == s256((int)CODE_TABLE_NOT_EXIST));

    // remove condition error
    conditionStr = "{\"name\"{\"eq\":\"fruit\"}}";
    param.clear();
    out.clear();
    param = codec->encodeWithSig(removeFunc, tableName, conditionStr, std::string(""));
    callResult = crudPrecompiled->call(context, bytesConstRef(&param), "","",gas);
    out = callResult->execResult();
    removeResult = 0;
    codec->decode(&out, removeResult);
    BOOST_TEST(removeResult == s256((int)CODE_PARSE_CONDITION_ERROR));

    // remove condition operation undefined
    conditionStr = "{\"name\":{\"eqq\":\"fruit\"}}";
    param.clear();
    out.clear();
    param = codec->encodeWithSig(removeFunc, tableName, conditionStr, std::string(""));
    callResult = crudPrecompiled->call(context, bytesConstRef(&param), "","",gas);
    out = callResult->execResult();
    removeResult = 0;
    codec->decode(&out, removeResult);
    BOOST_TEST(removeResult == s256((int)CODE_CONDITION_OPERATION_UNDEFINED));

    // function not exist
    std::string errorFunc = "errorFunc(string,string,string,string)";
    param = codec->encodeWithSig(errorFunc, tableName, key, conditionStr, std::string(""));
    callResult = crudPrecompiled->call(context, bytesConstRef(&param), "","",gas);
    out = callResult->execResult();
    s256 funcResult = 0;
    codec->decode(&out, funcResult);
    BOOST_TEST(funcResult == s256((int)CODE_UNKNOW_FUNCTION_CALL));
}

BOOST_AUTO_TEST_SUITE_END()
}