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

#include "precompiled/CRUDPrecompiled.h"
#include "libprecompiled/PreCompiledFixture.h"
#include "precompiled/ConditionPrecompiled.h"
#include "precompiled/EntriesPrecompiled.h"
#include "precompiled/EntryPrecompiled.h"
#include "precompiled/TableFactoryPrecompiled.h"
#include <bcos-framework/libutilities/Error.h>
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
        codec = std::make_shared<PrecompiledCodec>(hashImpl, false);
        setIsWasm(false);
        crudTestAddress = Address("0x420f853b49838bd3e9466c85a4cc3428c960dde2").hex();
    }

    virtual ~CRUDPrecompiledTest() {}
    bytes callFunc(protocol::BlockNumber _number, int _contextId, const std::string& method,
        const std::string& tableName, const std::string& secondStr, const std::string& thirdStr,
        int _errorCode = 0)
    {
        nextBlock(_number);
        bytes in =
            (method == updateFunc) ?
                codec->encodeWithSig(method, tableName, secondStr, thirdStr, std::string("")) :
                codec->encodeWithSig(method, tableName, secondStr, thirdStr);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(_contextId);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(crudTestAddress);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        // call precompiled
        result2->setSeq(1001);

        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->executeTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();
        commitBlock(_number);
        if (method == selectFunc)
        {
            return result3->data().toBytes();
        }
        BOOST_CHECK(result3->data().toBytes() == codec->encode(s256(_errorCode)));
        return {};
    };

    void deployTest()
    {
        bytes input;
        boost::algorithm::unhex(crudTestBin, std::back_inserter(input));
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", input, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));

        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);

        auto params = std::make_unique<NativeExecutionMessage>();
        params->setContextID(99);
        params->setSeq(1000);
        params->setDepth(0);

        params->setOrigin(sender);
        params->setFrom(sender);

        // toChecksumAddress(addressString, hashImpl);
        params->setTo(crudTestAddress);
        params->setStaticCall(false);
        params->setGasAvailable(gas);
        params->setData(input);
        params->setType(NativeExecutionMessage::TXHASH);
        params->setTransactionHash(hash);
        params->setCreate(true);

        NativeExecutionMessage paramsBak = *params;
        nextBlock(1);
        // --------------------------------
        // Create contract
        // --------------------------------

        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise;
        executor->executeTransaction(
            std::move(params), [&](bcos::Error::UniquePtr&& error,
                                   bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise.set_value(std::move(result));
            });

        auto result = executePromise.get_future().get();
        BOOST_CHECK(result);
        BOOST_CHECK_EQUAL(result->type(), ExecutionMessage::FINISHED);
        BOOST_CHECK_EQUAL(result->contextID(), 99);
        BOOST_CHECK_EQUAL(result->seq(), 1000);
        BOOST_CHECK_EQUAL(result->create(), false);
        BOOST_CHECK_EQUAL(result->newEVMContractAddress(), crudTestAddress);
        BOOST_CHECK_EQUAL(result->origin(), sender);
        BOOST_CHECK_EQUAL(result->from(), crudTestAddress);
        BOOST_CHECK(result->to() == sender);
        BOOST_CHECK_LT(result->gasAvailable(), gas);
        commitBlock(1);
    }

    std::string selectFunc = "select(string,string,string)";
    std::string insertFunc = "insert(string,string,string)";
    std::string updateFunc = "update(string,string,string,string)";
    std::string removeFunc = "remove(string,string,string)";
    std::string crudTestBin =
        "608060405234801561001057600080fd5b506110026000806101000a81548173ffffffffffffffffffffffffff"
        "ffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff1602179055506111b4806100"
        "626000396000f30060806040526004361061006d576000357c0100000000000000000000000000000000000000"
        "000000000000000000900463ffffffff16806306201393146100725780632fe99bdc146101e05780635d0d6d54"
        "146102e9578063b99c407914610437578063da40fa7714610586575b600080fd5b34801561007e57600080fd5b"
        "50610165600480360381019080803590602001908201803590602001908080601f016020809104026020016040"
        "519081016040528093929190818152602001838380828437820191505050505050919291929080359060200190"
        "8201803590602001908080601f0160208091040260200160405190810160405280939291908181526020018383"
        "808284378201915050505050509192919290803590602001908201803590602001908080601f01602080910402"
        "602001604051908101604052809392919081815260200183838082843782019150505050505091929192905050"
        "5061068f565b6040518080602001828103825283818151815260200191508051906020019080838360005b8381"
        "10156101a557808201518184015260208101905061018a565b50505050905090810190601f1680156101d25780"
        "820380516001836020036101000a031916815260200191505b509250505060405180910390f35b3480156101ec"
        "57600080fd5b506102d3600480360381019080803590602001908201803590602001908080601f016020809104"
        "026020016040519081016040528093929190818152602001838380828437820191505050505050919291929080"
        "3590602001908201803590602001908080601f0160208091040260200160405190810160405280939291908181"
        "526020018383808284378201915050505050509192919290803590602001908201803590602001908080601f01"
        "602080910402602001604051908101604052809392919081815260200183838082843782019150505050505091"
        "929192905050506108fa565b6040518082815260200191505060405180910390f35b3480156102f557600080fd"
        "5b50610350600480360381019080803590602001908201803590602001908080601f0160208091040260200160"
        "405190810160405280939291908181526020018383808284378201915050505050509192919290505050610b0f"
        "565b604051808060200180602001838103835285818151815260200191508051906020019080838360005b8381"
        "1015610394578082015181840152602081019050610379565b50505050905090810190601f1680156103c15780"
        "820380516001836020036101000a031916815260200191505b5083810382528481815181526020019150805190"
        "6020019080838360005b838110156103fa5780820151818401526020810190506103df565b5050505090509081"
        "0190601f1680156104275780820380516001836020036101000a031916815260200191505b5094505050505060"
        "405180910390f35b34801561044357600080fd5b50610570600480360381019080803590602001908201803590"
        "602001908080601f01602080910402602001604051908101604052809392919081815260200183838082843782"
        "01915050505050509192919290803590602001908201803590602001908080601f016020809104026020016040"
        "519081016040528093929190818152602001838380828437820191505050505050919291929080359060200190"
        "8201803590602001908080601f0160208091040260200160405190810160405280939291908181526020018383"
        "808284378201915050505050509192919290803590602001908201803590602001908080601f01602080910402"
        "602001604051908101604052809392919081815260200183838082843782019150505050505091929192905050"
        "50610cf0565b6040518082815260200191505060405180910390f35b34801561059257600080fd5b5061067960"
        "0480360381019080803590602001908201803590602001908080601f0160208091040260200160405190810160"
        "405280939291908181526020018383808284378201915050505050509192919290803590602001908201803590"
        "602001908080601f01602080910402602001604051908101604052809392919081815260200183838082843782"
        "01915050505050509192919290803590602001908201803590602001908080601f016020809104026020016040"
        "5190810160405280939291908181526020018383808284378201915050505050509192919290505050610f7356"
        "5b6040518082815260200191505060405180910390f35b60606000809054906101000a900473ffffffffffffff"
        "ffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16630620139385858560"
        "40518463ffffffff167c0100000000000000000000000000000000000000000000000000000000028152600401"
        "80806020018060200180602001848103845287818151815260200191508051906020019080838360005b838110"
        "15610744578082015181840152602081019050610729565b50505050905090810190601f168015610771578082"
        "0380516001836020036101000a031916815260200191505b508481038352868181518152602001915080519060"
        "20019080838360005b838110156107aa57808201518184015260208101905061078f565b505050509050908101"
        "90601f1680156107d75780820380516001836020036101000a031916815260200191505b508481038252858181"
        "51815260200191508051906020019080838360005b838110156108105780820151818401526020810190506107"
        "f5565b50505050905090810190601f16801561083d5780820380516001836020036101000a0319168152602001"
        "91505b509650505050505050600060405180830381600087803b15801561086057600080fd5b505af115801561"
        "0874573d6000803e3d6000fd5b505050506040513d6000823e3d601f19601f8201168201806040525060208110"
        "1561089e57600080fd5b8101908080516401000000008111156108b657600080fd5b8281019050602081018481"
        "11156108cc57600080fd5b81518560018202830111640100000000821117156108e957600080fd5b5050929190"
        "50505090509392505050565b60008060009054906101000a900473ffffffffffffffffffffffffffffffffffff"
        "ffff1673ffffffffffffffffffffffffffffffffffffffff16632fe99bdc8585856040518463ffffffff167c01"
        "000000000000000000000000000000000000000000000000000000000281526004018080602001806020018060"
        "2001848103845287818151815260200191508051906020019080838360005b838110156109af57808201518184"
        "0152602081019050610994565b50505050905090810190601f1680156109dc5780820380516001836020036101"
        "000a031916815260200191505b50848103835286818151815260200191508051906020019080838360005b8381"
        "1015610a155780820151818401526020810190506109fa565b50505050905090810190601f168015610a425780"
        "820380516001836020036101000a031916815260200191505b5084810382528581815181526020019150805190"
        "6020019080838360005b83811015610a7b578082015181840152602081019050610a60565b5050505090509081"
        "0190601f168015610aa85780820380516001836020036101000a031916815260200191505b5096505050505050"
        "50602060405180830381600087803b158015610acb57600080fd5b505af1158015610adf573d6000803e3d6000"
        "fd5b505050506040513d6020811015610af557600080fd5b810190808051906020019092919050505090509392"
        "505050565b6060806000809054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffff"
        "ffffffffffffffffffffffffffffffffffff16635d0d6d54846040518263ffffffff167c010000000000000000"
        "000000000000000000000000000000000000000002815260040180806020018281038252838181518152602001"
        "91508051906020019080838360005b83811015610bbb578082015181840152602081019050610ba0565b505050"
        "50905090810190601f168015610be85780820380516001836020036101000a031916815260200191505b509250"
        "5050600060405180830381600087803b158015610c0757600080fd5b505af1158015610c1b573d6000803e3d60"
        "00fd5b505050506040513d6000823e3d601f19601f820116820180604052506040811015610c4557600080fd5b"
        "810190808051640100000000811115610c5d57600080fd5b82810190506020810184811115610c7357600080fd"
        "5b8151856001820283011164010000000082111715610c9057600080fd5b505092919060200180516401000000"
        "00811115610cac57600080fd5b82810190506020810184811115610cc257600080fd5b81518560018202830111"
        "64010000000082111715610cdf57600080fd5b505092919050505091509150915091565b600080600090549061"
        "01000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffff"
        "ffffff1663b99c4079868686866040518563ffffffff167c010000000000000000000000000000000000000000"
        "000000000000000002815260040180806020018060200180602001806020018581038552898181518152602001"
        "91508051906020019080838360005b83811015610daa578082015181840152602081019050610d8f565b505050"
        "50905090810190601f168015610dd75780820380516001836020036101000a031916815260200191505b508581"
        "03845288818151815260200191508051906020019080838360005b83811015610e105780820151818401526020"
        "81019050610df5565b50505050905090810190601f168015610e3d5780820380516001836020036101000a0319"
        "16815260200191505b50858103835287818151815260200191508051906020019080838360005b83811015610e"
        "76578082015181840152602081019050610e5b565b50505050905090810190601f168015610ea3578082038051"
        "6001836020036101000a031916815260200191505b508581038252868181518152602001915080519060200190"
        "80838360005b83811015610edc578082015181840152602081019050610ec1565b50505050905090810190601f"
        "168015610f095780820380516001836020036101000a031916815260200191505b509850505050505050505060"
        "2060405180830381600087803b158015610f2e57600080fd5b505af1158015610f42573d6000803e3d6000fd5b"
        "505050506040513d6020811015610f5857600080fd5b8101908080519060200190929190505050905094935050"
        "5050565b60008060009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffff"
        "ffffffffffffffffffffffffffffffff1663da40fa778585856040518463ffffffff167c010000000000000000"
        "000000000000000000000000000000000000000002815260040180806020018060200180602001848103845287"
        "818151815260200191508051906020019080838360005b83811015611028578082015181840152602081019050"
        "61100d565b50505050905090810190601f1680156110555780820380516001836020036101000a031916815260"
        "200191505b50848103835286818151815260200191508051906020019080838360005b8381101561108e578082"
        "015181840152602081019050611073565b50505050905090810190601f1680156110bb57808203805160018360"
        "20036101000a031916815260200191505b50848103825285818151815260200191508051906020019080838360"
        "005b838110156110f45780820151818401526020810190506110d9565b50505050905090810190601f16801561"
        "11215780820380516001836020036101000a031916815260200191505b50965050505050505060206040518083"
        "0381600087803b15801561114457600080fd5b505af1158015611158573d6000803e3d6000fd5b505050506040"
        "513d602081101561116e57600080fd5b8101908080519060200190929190505050905093925050505600a16562"
        "7a7a723058205c8d5d33104804975eac340b6ef91a0212b14082d0bc88ba5080f557b9ad5efd0029";
    std::string crudTestAddress;
    std::string sender;
};
BOOST_FIXTURE_TEST_SUITE(precompiledCRUDTest, CRUDPrecompiledTest)

BOOST_AUTO_TEST_CASE(CRUD_simple_evm)
{
    deployTest();

    // createTable
    std::string tableName = "t_test", tableName2 = "t_demo", key = "name",
                valueField = "item_id,item_name";
    std::promise<std::optional<Table>> tablePromise;
    storage->asyncCreateTable(getTableName(tableName), valueField + "," + key,
        [&](Error::UniquePtr&& _error, std::optional<Table>&& _table) {
            BOOST_CHECK(!_error);
            tablePromise.set_value(std::move(_table));
        });
    auto table = tablePromise.get_future().get();
    BOOST_CHECK(table != std::nullopt);

    // desc
    {
        nextBlock(2);
        bytes in = codec->encodeWithSig("desc(string)", tableName);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(100);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(crudTestAddress);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        // call precompiled
        result2->setSeq(1001);

        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->executeTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();
        BOOST_CHECK(result3->data().toBytes() == codec->encode(key, valueField));
        commitBlock(2);
    }

    // desc not exist
    {
        nextBlock(3);
        bytes in = codec->encodeWithSig("desc(string)", std::string("error"));
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(101);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(crudTestAddress);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        // call precompiled registerParallelFunctionInternal
        result2->setSeq(1001);

        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->executeTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();
        BOOST_CHECK(result3->data().toBytes() == codec->encode(std::string(""), std::string("")));
        commitBlock(3);
    }

    // insert
    std::string entryStr = "{\"item_id\":\"1\",\"name\":\"fruit\",\"item_name\":\"apple\"}";
    {
        callFunc(4, 102, insertFunc, tableName, entryStr, std::string(""), 1);
    }

    // insert table not exist
    entryStr = "{\"item_id\":\"1\",\"name\":\"fruit\",\"item_name\":\"apple\"}";
    {
        callFunc(5, 103, insertFunc, tableName2, entryStr, std::string(""), CODE_TABLE_NOT_EXIST);
    }

    // insert entry error
    entryStr = "{\"item_id\"1\",\"name\":\"fruit\",\"item_name\":\"apple\"}";
    {
        callFunc(6, 104, insertFunc, tableName, entryStr, std::string(""), CODE_PARSE_ENTRY_ERROR);
    }

    // select
    std::string conditionStr =
        "{\"name\":{\"eq\":\"fruit\"},\"item_id\":{\"eq\":\"1\"},\"limit\":{\"limit\":\"0,1\"}}";
    {
        std::string selectResult;
        bytes result = callFunc(7, 105, selectFunc, tableName, conditionStr, std::string(""));
        codec->decode(ref(result), selectResult);
        Json::Value entryJson;
        Json::Reader reader;
        reader.parse(selectResult, entryJson);
        BOOST_TEST(entryJson.size() == 1);
    }

    // select table not exist
    {
        bytes result = callFunc(8, 106, selectFunc, tableName2, conditionStr, std::string(""));
        s256 selectResult2 = 0;
        codec->decode(ref(result), selectResult2);
        BOOST_TEST(selectResult2 == s256((int)CODE_TABLE_NOT_EXIST));
    }

    // select condition error
    conditionStr = "{\"item_id\":\"eq\":\"1\"},\"limit\":{\"limit\":\"0,1\"}}";
    {
        bytes result = callFunc(9, 107, selectFunc, tableName, conditionStr, std::string(""));
        s256 selectResult2 = 0;
        codec->decode(ref(result), selectResult2);
        BOOST_TEST(selectResult2 == s256((int)CODE_PARSE_CONDITION_ERROR));
    }

    // update
    entryStr = "{\"item_id\":\"1\",\"item_name\":\"orange\"}";
    conditionStr = "{\"name\":{\"eq\":\"fruit\"}}";
    {
        callFunc(10, 108, updateFunc, tableName, entryStr, conditionStr, 1);
    }

    // update table not exist
    entryStr = "{\"item_id\":\"1\",\"item_name\":\"orange\"}";
    conditionStr = "{\"name\":{\"eq\":\"fruit\"}}";
    {
        callFunc(11, 109, updateFunc, tableName2, entryStr, conditionStr, CODE_TABLE_NOT_EXIST);
    }

    // update entry error
    entryStr = "{\"item_id\"1\",\"item_name\":\"apple\"}";
    conditionStr = "{\"name\":{\"eq\":\"fruit\"}}";
    {
        callFunc(12, 110, updateFunc, tableName, entryStr, conditionStr, CODE_PARSE_ENTRY_ERROR);
    }

    // update condition error
    entryStr = "{\"item_id\":\"1\",\"item_name\":\"orange\"}";
    conditionStr = "{\"name\"\"eq\":\"fruit\"}}";
    {
        callFunc(
            13, 111, updateFunc, tableName, entryStr, conditionStr, CODE_PARSE_CONDITION_ERROR);
    }

    // remove
    conditionStr = "{\"name\":{\"eq\":\"fruit\"}}";
    {
        callFunc(14, 112, removeFunc, tableName, conditionStr, std::string(""), 1);
    }

    // remove table not exist
    conditionStr = "{\"name\":{\"eq\":\"fruit\"}}";
    {
        callFunc(
            15, 113, removeFunc, tableName2, conditionStr, std::string(""), CODE_TABLE_NOT_EXIST);
    }

    // remove condition error
    conditionStr = "{\"name\"{\"eq\":\"fruit\"}}";
    {
        callFunc(16, 114, removeFunc, tableName, conditionStr, std::string(""),
            CODE_PARSE_CONDITION_ERROR);
    }

    // remove condition operation undefined
    conditionStr = "{\"name\":{\"eqq\":\"fruit\"}}";
    {
        callFunc(17, 115, removeFunc, tableName, conditionStr, std::string(""),
            CODE_CONDITION_OPERATION_UNDEFINED);
    }
}

BOOST_AUTO_TEST_CASE(CRUD_boundary_evm)
{
    deployTest();
    auto createTable = [&](std::string tableName) {
        std::string key = "name", valueField = "item_id,item_name";
        std::promise<std::optional<Table>> tablePromise;
        storage->asyncCreateTable(getTableName(tableName), valueField + "," + key,
            [&](Error::UniquePtr&& _error, std::optional<Table>&& _table) {
                BOOST_CHECK(!_error);
                tablePromise.set_value(std::move(_table));
            });
        auto table = tablePromise.get_future().get();
        BOOST_CHECK(table != std::nullopt);
    };
    // test insert part entry
    {
        // createTable
        std::string tableName = "t_test1";
        createTable(tableName);
        bytes param, out;

        // insert
        std::string entryStr = "{\"item_id\":\"1\",\"name\":\"fruit\",\"item_name\":\"apple\"}";
        callFunc(2, 100, insertFunc, tableName, entryStr, std::string(""), 1);

        // insert exist entry
        entryStr = "{\"item_id\":\"1\",\"name\":\"fruit\",\"item_name\":\"apple\"}";
        callFunc(3, 101, insertFunc, tableName, entryStr, std::string(""), CODE_INSERT_KEY_EXIST);

        // insert part entry
        entryStr = "{\"name\":\"fruit2\",\"item_name\":\"apple\"}";
        callFunc(4, 102, insertFunc, tableName, entryStr, std::string(""), 1);

        // select fruit
        std::string conditionStr = "{\"name\":{\"eq\":\"fruit\"},\"item_id\":{\"eq\":\"1\"}}";
        out = callFunc(5, 103, selectFunc, tableName, conditionStr, std::string(""));
        std::string selectResult;
        codec->decode(&out, selectResult);
        Json::Value entryJson;
        Json::Reader reader;
        reader.parse(selectResult, entryJson);
        BOOST_TEST(entryJson.size() == 1);

        // select part insert fruit2
        conditionStr = "{\"name\":{\"eq\":\"fruit2\"},\"item_name\":{\"eq\":\"apple\"}}";
        out.clear();
        out = callFunc(6, 104, selectFunc, tableName, conditionStr, std::string(""));
        codec->decode(&out, selectResult);
        reader.parse(selectResult, entryJson);
        BOOST_TEST(entryJson.size() == 1u);
        BOOST_TEST(entryJson[0]["item_id"].asString() == "");
    }

    // test update part entry
    {
        // createTable
        std::string tableName = "t_test2";
        createTable(tableName);
        bytes param, out;

        // insert
        std::string entryStr = "{\"item_id\":\"1\",\"name\":\"fruit\",\"item_name\":\"apple\"}";
        callFunc(7, 105, insertFunc, tableName, entryStr, std::string(""), 1);

        // select
        std::string conditionStr = "{\"name\":{\"eq\":\"fruit\"},\"item_id\":{\"eq\":\"1\"}}";
        out.clear();
        out = callFunc(8, 106, selectFunc, tableName, conditionStr, std::string(""));
        std::string selectResult;
        codec->decode(&out, selectResult);
        Json::Value entryJson;
        Json::Reader reader;
        reader.parse(selectResult, entryJson);
        BOOST_TEST(entryJson.size() == 1);

        // update not found condition
        entryStr = "{\"item_name\":\"orange\"}";
        conditionStr = "{\"name\":{\"eq\":\"fruit\"},\"item_id\":{\"eq\":\"123\"}}";
        callFunc(9, 107, updateFunc, tableName, entryStr, conditionStr, 0);

        // update part entry
        entryStr = "{\"item_name\":\"orange\"}";
        conditionStr = "{\"name\":{\"eq\":\"fruit\"}}";
        callFunc(10, 108, updateFunc, tableName, entryStr, conditionStr, 1);

        conditionStr = "{\"name\":{\"eq\":\"fruit\"},\"item_id\":{\"eq\":\"1\"}}";
        out.clear();
        out = callFunc(11, 109, selectFunc, tableName, conditionStr, std::string(""));
        codec->decode(&out, selectResult);
        reader.parse(selectResult, entryJson);
        BOOST_TEST(entryJson.size() == 1);
    }

    // test remove condition
    {
        // createTable
        std::string tableName = "t_test3";
        createTable(tableName);
        bytes param, out;

        // insert
        std::string entryStr = "{\"item_id\":\"1\",\"name\":\"fruit\",\"item_name\":\"apple\"}";
        callFunc(12, 110, insertFunc, tableName, entryStr, std::string(""), 1);

        // select
        std::string conditionStr = "{\"name\":{\"eq\":\"fruit\"},\"item_id\":{\"eq\":\"1\"}}";
        out.clear();
        out = callFunc(13, 111, selectFunc, tableName, conditionStr, std::string(""));
        std::string selectResult;
        codec->decode(&out, selectResult);
        Json::Value entryJson;
        Json::Reader reader;
        reader.parse(selectResult, entryJson);
        BOOST_TEST(entryJson.size() == 1);

        // update part entry
        entryStr = "{\"item_name\":\"orange\"}";
        conditionStr = "{\"name\":{\"eq\":\"fruit\"}}";
        callFunc(14, 112, updateFunc, tableName, entryStr, conditionStr, 1);

        // remove condition not found
        conditionStr = "{\"name\":{\"eq\":\"fruit\"}, \"item_id\":{\"eq\":\"123\"}}";
        callFunc(15, 113, removeFunc, tableName, conditionStr, std::string(""), 0);

        // remove
        conditionStr = "{\"name\":{\"eq\":\"fruit\"}, \"item_id\":{\"eq\":\"1\"}}";
        callFunc(16, 114, removeFunc, tableName, conditionStr, std::string(""), 1);
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test