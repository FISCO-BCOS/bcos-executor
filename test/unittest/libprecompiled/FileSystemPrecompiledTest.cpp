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
 * @file FileSystemPrecompiledTest.cpp
 * @author: kyonRay
 * @date 2021-06-20
 */

#include "precompiled/FileSystemPrecompiled.h"
#include "PreCompiledFixture.h"
#include "precompiled/KVTableFactoryPrecompiled.h"
#include <bcos-framework/testutils/TestPromptFixture.h>
#include <json/json.h>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::ledger;

namespace bcos::test
{
class FileSystemPrecompiledFixture : public PrecompiledFixture
{
public:
    FileSystemPrecompiledFixture()
    {
        kvTableFactoryPrecompiled = std::make_shared<KVTableFactoryPrecompiled>(hashImpl);
        fileSystemPrecompiled = std::make_shared<FileSystemPrecompiled>(hashImpl);
        setIsWasm(true);
        kvTableFactoryPrecompiled->setMemoryTableFactory(context->storage());

        // create table test1 test2, there are two data files in /tables/
        bytes param = codec->encodeWithSig("createTable(string,string,string)",
            std::string("test1"), std::string("id"), std::string("item_name,item_id"));
        kvTableFactoryPrecompiled->call(context, bytesConstRef(&param), "", "");
        param = codec->encodeWithSig("createTable(string,string,string)", std::string("test2"),
            std::string("id"), std::string("item_name,item_id"));
        kvTableFactoryPrecompiled->call(context, bytesConstRef(&param), "", "");
    }

    virtual ~FileSystemPrecompiledFixture() {}

    FileSystemPrecompiled::Ptr fileSystemPrecompiled;
    KVTableFactoryPrecompiled::Ptr kvTableFactoryPrecompiled;
};
BOOST_FIXTURE_TEST_SUITE(precompiledFileSystemTest, FileSystemPrecompiledFixture)

BOOST_AUTO_TEST_CASE(toString)
{
    BOOST_CHECK_EQUAL(fileSystemPrecompiled->toString(), "FileSystem");
}

BOOST_AUTO_TEST_CASE(lsTest)
{
    // ls dir
    bytes in = codec->encodeWithSig("list(string)", std::string("/tables"));
    auto callResult = fileSystemPrecompiled->call(context, bytesConstRef(&in), "test", "");
    bytes out = callResult->execResult();
    std::string result;
    codec->decode(&out, result);
    std::cout << result << std::endl;
    Json::Value retJson;
    Json::Reader reader;
    BOOST_CHECK(reader.parse(result, retJson) == true);
    BOOST_CHECK(retJson.size() == 2);

    // ls regular
    bytes in2 = codec->encodeWithSig("list(string)", std::string("/tables/test2"));
    callResult = fileSystemPrecompiled->call(context, bytesConstRef(&in2), "", "");
    out = callResult->execResult();
    codec->decode(&out, result);
    std::cout << result << std::endl;
    BOOST_CHECK(reader.parse(result, retJson) == true);
    BOOST_CHECK(retJson.size() == 1);
    BOOST_CHECK(retJson[0][FS_FIELD_TYPE] == FS_TYPE_CONTRACT);

    // ls not exist
    bytes in3 = codec->encodeWithSig("list(string)", std::string("/data/test3"));
    callResult = fileSystemPrecompiled->call(context, bytesConstRef(&in3), "", "");
    out = callResult->execResult();
    s256 errorCode;
    codec->decode(&out, errorCode);
    BOOST_CHECK(errorCode == s256((int)CODE_FILE_NOT_EXIST));

    // ls /
    bytes in4 = codec->encodeWithSig("list(string)", std::string("/"));
    callResult = fileSystemPrecompiled->call(context, bytesConstRef(&in4), "", "");
    out = callResult->execResult();
    codec->decode(&out, result);
    std::cout << result << std::endl;
    BOOST_CHECK(reader.parse(result, retJson) == true);
    BOOST_CHECK(retJson.size() == 2);
    BOOST_CHECK(retJson[0][FS_FIELD_TYPE] == FS_TYPE_DIR);
}

BOOST_AUTO_TEST_CASE(mkdirTest)
{
    bytes in = codec->encodeWithSig("mkdir(string)", std::string("/tables/temp/test"));
    auto callResult = fileSystemPrecompiled->call(context, bytesConstRef(&in), "", "");
    bytes out = callResult->execResult();
    u256 result;
    codec->decode(&out, result);
    BOOST_TEST(result == 0u);

    // mkdir /data/test1/test
    in = codec->encodeWithSig("mkdir(string)", std::string("/tables/test1/test"));
    callResult = fileSystemPrecompiled->call(context, bytesConstRef(&in), "", "");
    out = callResult->execResult();
    s256 errorCode;
    codec->decode(&out, errorCode);
    BOOST_TEST(errorCode == s256((int)CODE_FILE_BUILD_DIR_FAILED));

    // mkdir /data/test1
    in = codec->encodeWithSig("mkdir(string)", std::string("/tables/test1"));
    callResult = fileSystemPrecompiled->call(context, bytesConstRef(&in), "", "");
    out = callResult->execResult();
    codec->decode(&out, errorCode);
    BOOST_TEST(errorCode == s256((int)CODE_FILE_ALREADY_EXIST));

    // mkdir /data
    in = codec->encodeWithSig("mkdir(string)", std::string("/tables"));
    callResult = fileSystemPrecompiled->call(context, bytesConstRef(&in), "", "");
    out = callResult->execResult();
    codec->decode(&out, errorCode);
    BOOST_TEST(errorCode == s256((int)CODE_FILE_ALREADY_EXIST));
}

BOOST_AUTO_TEST_CASE(undefined_test)
{
    bytes in = codec->encodeWithSig("take(string)");
    fileSystemPrecompiled->call(context, bytesConstRef(&in), "", "");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test