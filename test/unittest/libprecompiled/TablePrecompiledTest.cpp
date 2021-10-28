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
 * @file TableFactoryPrecompiledTest.cpp
 * @author: kyonRay
 * @date 2021-06-21
 */

#include "precompiled/TableFactoryPrecompiled.h"
#include "libprecompiled/PreCompiledFixture.h"
#include <bcos-framework/testutils/TestPromptFixture.h>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::ledger;

namespace bcos::test
{
class TableFactoryPrecompiledFixture : public PrecompiledFixture
{
public:
    TableFactoryPrecompiledFixture()
    {
        codec = std::make_shared<PrecompiledCodec>(hashImpl, false);
        setIsWasm(false);
        tableTestAddress = Address("0x420f853b49838bd3e9466c85a4cc3428c960dde2").hex();
    }

    virtual ~TableFactoryPrecompiledFixture() {}

    void deployTest()
    {
        bytes input;
        boost::algorithm::unhex(tableTestBin, std::back_inserter(input));
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
        params->setTo(tableTestAddress);
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
        BOOST_CHECK_EQUAL(result->newEVMContractAddress(), tableTestAddress);
        BOOST_CHECK_EQUAL(result->origin(), sender);
        BOOST_CHECK_EQUAL(result->from(), tableTestAddress);
        BOOST_CHECK(result->to() == sender);
        BOOST_CHECK_LT(result->gasAvailable(), gas);
        commitBlock(1);
    }

    ExecutionMessage::UniquePtr creatTable(bool isKV, protocol::BlockNumber _number, int _contextId,
        const std::string& tableName, const std::string& key, const std::string& value,
        int _errorCode = 0)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig(
            isKV ? "createKVTable(string,string,string)" : "createTable(string,string,string)",
            tableName, key, value);
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
        params2->setTo(tableTestAddress);
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
        if (_errorCode != 0)
        {
            BOOST_CHECK(result3->data().toBytes() == codec->encode(s256(_errorCode)));
        }
        commitBlock(_number);
        return result3;
    };

    ExecutionMessage::UniquePtr openTable(bool isKV, int _contextId, const std::string& tableName)
    {
        bytes in =
            codec->encodeWithSig(isKV ? "openKVTable(string)" : "openTable(string)", tableName);
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
        params2->setTo(tableTestAddress);
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
        return result3;
    };

    void initTable(){
        creatTable(false,2,100,"test","id","name,age");
        nextBlock(3);
        auto result = openTable(false, 101, "t_test");
        Address tableAddress;
        codec->decode(result->data(), tableAddress);

    }

    std::string tableTestBin = "608060405234801561001057600080fd5b506110016000806101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff160217905550611009600160006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff16021790555061185b806100a56000396000f3006080604052600436106100c5576000357c0100000000000000000000000000000000000000000000000000000000900463ffffffff1680631eb42523146100ca57806325e0563e146101615780632eca25b1146101e457806356004b6a146102a157806359839041146103aa5780637001f7131461044d5780637f7c1491146104d05780638661ffb6146105475780639537593a146105f0578063b0e89adb14610673578063c0a2203e1461077c578063f23f63c9146107f3578063fc2525ab1461089c575b600080fd5b3480156100d657600080fd5b5061014b600480360381019080803573ffffffffffffffffffffffffffffffffffffffff169060200190929190803573ffffffffffffffffffffffffffffffffffffffff169060200190929190803573ffffffffffffffffffffffffffffffffffffffff169060200190929190505050610970565b6040518082815260200191505060405180910390f35b34801561016d57600080fd5b506101a2600480360381019080803573ffffffffffffffffffffffffffffffffffffffff169060200190929190505050610a85565b604051808273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200191505060405180910390f35b3480156101f057600080fd5b5061028b600480360381019080803573ffffffffffffffffffffffffffffffffffffffff169060200190929190803590602001908201803590602001908080601f0160208091040260200160405190810160405280939291908181526020018383808284378201915050505050509192919290803573ffffffffffffffffffffffffffffffffffffffff169060200190929190505050610b2d565b6040518082815260200191505060405180910390f35b3480156102ad57600080fd5b50610394600480360381019080803590602001908201803590602001908080601f0160208091040260200160405190810160405280939291908181526020018383808284378201915050505050509192919290803590602001908201803590602001908080601f0160208091040260200160405190810160405280939291908181526020018383808284378201915050505050509192919290803590602001908201803590602001908080601f0160208091040260200160405190810160405280939291908181526020018383808284378201915050505050509192919290505050610c7b565b6040518082815260200191505060405180910390f35b3480156103b657600080fd5b5061040b600480360381019080803573ffffffffffffffffffffffffffffffffffffffff169060200190929190803573ffffffffffffffffffffffffffffffffffffffff169060200190929190505050610e90565b604051808273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200191505060405180910390f35b34801561045957600080fd5b5061048e600480360381019080803573ffffffffffffffffffffffffffffffffffffffff169060200190929190505050610f70565b604051808273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200191505060405180910390f35b3480156104dc57600080fd5b50610531600480360381019080803573ffffffffffffffffffffffffffffffffffffffff169060200190929190803573ffffffffffffffffffffffffffffffffffffffff169060200190929190505050611018565b6040518082815260200191505060405180910390f35b34801561055357600080fd5b506105ae600480360381019080803590602001908201803590602001908080601f01602080910402602001604051908101604052809392919081815260200183838082843782019150505050505091929192905050506110f8565b604051808273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200191505060405180910390f35b3480156105fc57600080fd5b50610631600480360381019080803573ffffffffffffffffffffffffffffffffffffffff169060200190929190505050611232565b604051808273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200191505060405180910390f35b34801561067f57600080fd5b50610766600480360381019080803590602001908201803590602001908080601f0160208091040260200160405190810160405280939291908181526020018383808284378201915050505050509192919290803590602001908201803590602001908080601f0160208091040260200160405190810160405280939291908181526020018383808284378201915050505050509192919290803590602001908201803590602001908080601f01602080910402602001604051908101604052809392919081815260200183838082843782019150505050505091929192905050506112da565b6040518082815260200191505060405180910390f35b34801561078857600080fd5b506107dd600480360381019080803573ffffffffffffffffffffffffffffffffffffffff169060200190929190803573ffffffffffffffffffffffffffffffffffffffff1690602001909291905050506114f0565b6040518082815260200191505060405180910390f35b3480156107ff57600080fd5b5061085a600480360381019080803590602001908201803590602001908080601f01602080910402602001604051908101604052809392919081815260200183838082843782019150505050505091929192905050506115d0565b604051808273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200191505060405180910390f35b3480156108a857600080fd5b50610923600480360381019080803573ffffffffffffffffffffffffffffffffffffffff169060200190929190803590602001908201803590602001908080601f0160208091040260200160405190810160405280939291908181526020018383808284378201915050505050509192919290505050611709565b60405180831515151581526020018273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019250505060405180910390f35b60008373ffffffffffffffffffffffffffffffffffffffff1663c640752d84846040518363ffffffff167c0100000000000000000000000000000000000000000000000000000000028152600401808373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020018273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200192505050602060405180830381600087803b158015610a4157600080fd5b505af1158015610a55573d6000803e3d6000fd5b505050506040513d6020811015610a6b57600080fd5b810190808051906020019092919050505090509392505050565b60008173ffffffffffffffffffffffffffffffffffffffff166313db93466040518163ffffffff167c0100000000000000000000000000000000000000000000000000000000028152600401602060405180830381600087803b158015610aeb57600080fd5b505af1158015610aff573d6000803e3d6000fd5b505050506040513d6020811015610b1557600080fd5b81019080805190602001909291905050509050919050565b60008373ffffffffffffffffffffffffffffffffffffffff1663a815ff1584846040518363ffffffff167c010000000000000000000000000000000000000000000000000000000002815260040180806020018373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001828103825284818151815260200191508051906020019080838360005b83811015610bea578082015181840152602081019050610bcf565b50505050905090810190601f168015610c175780820380516001836020036101000a031916815260200191505b509350505050602060405180830381600087803b158015610c3757600080fd5b505af1158015610c4b573d6000803e3d6000fd5b505050506040513d6020811015610c6157600080fd5b810190808051906020019092919050505090509392505050565b60008060009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff166356004b6a8585856040518463ffffffff167c010000000000000000000000000000000000000000000000000000000002815260040180806020018060200180602001848103845287818151815260200191508051906020019080838360005b83811015610d30578082015181840152602081019050610d15565b50505050905090810190601f168015610d5d5780820380516001836020036101000a031916815260200191505b50848103835286818151815260200191508051906020019080838360005b83811015610d96578082015181840152602081019050610d7b565b50505050905090810190601f168015610dc35780820380516001836020036101000a031916815260200191505b50848103825285818151815260200191508051906020019080838360005b83811015610dfc578082015181840152602081019050610de1565b50505050905090810190601f168015610e295780820380516001836020036101000a031916815260200191505b509650505050505050602060405180830381600087803b158015610e4c57600080fd5b505af1158015610e60573d6000803e3d6000fd5b505050506040513d6020811015610e7657600080fd5b810190808051906020019092919050505090509392505050565b60008273ffffffffffffffffffffffffffffffffffffffff16634f49f01c836040518263ffffffff167c0100000000000000000000000000000000000000000000000000000000028152600401808273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001915050602060405180830381600087803b158015610f2d57600080fd5b505af1158015610f41573d6000803e3d6000fd5b505050506040513d6020811015610f5757600080fd5b8101908080519060200190929190505050905092915050565b60008173ffffffffffffffffffffffffffffffffffffffff16637857d7c96040518163ffffffff167c0100000000000000000000000000000000000000000000000000000000028152600401602060405180830381600087803b158015610fd657600080fd5b505af1158015610fea573d6000803e3d6000fd5b505050506040513d602081101561100057600080fd5b81019080805190602001909291905050509050919050565b60008273ffffffffffffffffffffffffffffffffffffffff166329092d0e836040518263ffffffff167c0100000000000000000000000000000000000000000000000000000000028152600401808273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001915050602060405180830381600087803b1580156110b557600080fd5b505af11580156110c9573d6000803e3d6000fd5b505050506040513d60208110156110df57600080fd5b8101908080519060200190929190505050905092915050565b6000600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1663f23f63c9836040518263ffffffff167c01000000000000000000000000000000000000000000000000000000000281526004018080602001828103825283818151815260200191508051906020019080838360005b838110156111a4578082015181840152602081019050611189565b50505050905090810190601f1680156111d15780820380516001836020036101000a031916815260200191505b5092505050602060405180830381600087803b1580156111f057600080fd5b505af1158015611204573d6000803e3d6000fd5b505050506040513d602081101561121a57600080fd5b81019080805190602001909291905050509050919050565b60008173ffffffffffffffffffffffffffffffffffffffff166313db93466040518163ffffffff167c0100000000000000000000000000000000000000000000000000000000028152600401602060405180830381600087803b15801561129857600080fd5b505af11580156112ac573d6000803e3d6000fd5b505050506040513d60208110156112c257600080fd5b81019080805190602001909291905050509050919050565b6000600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff166356004b6a8585856040518463ffffffff167c010000000000000000000000000000000000000000000000000000000002815260040180806020018060200180602001848103845287818151815260200191508051906020019080838360005b83811015611390578082015181840152602081019050611375565b50505050905090810190601f1680156113bd5780820380516001836020036101000a031916815260200191505b50848103835286818151815260200191508051906020019080838360005b838110156113f65780820151818401526020810190506113db565b50505050905090810190601f1680156114235780820380516001836020036101000a031916815260200191505b50848103825285818151815260200191508051906020019080838360005b8381101561145c578082015181840152602081019050611441565b50505050905090810190601f1680156114895780820380516001836020036101000a031916815260200191505b509650505050505050602060405180830381600087803b1580156114ac57600080fd5b505af11580156114c0573d6000803e3d6000fd5b505050506040513d60208110156114d657600080fd5b810190808051906020019092919050505090509392505050565b60008273ffffffffffffffffffffffffffffffffffffffff1663bc902ad2836040518263ffffffff167c0100000000000000000000000000000000000000000000000000000000028152600401808273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001915050602060405180830381600087803b15801561158d57600080fd5b505af11580156115a1573d6000803e3d6000fd5b505050506040513d60208110156115b757600080fd5b8101908080519060200190929190505050905092915050565b60008060009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1663f23f63c9836040518263ffffffff167c01000000000000000000000000000000000000000000000000000000000281526004018080602001828103825283818151815260200191508051906020019080838360005b8381101561167b578082015181840152602081019050611660565b50505050905090810190601f1680156116a85780820380516001836020036101000a031916815260200191505b5092505050602060405180830381600087803b1580156116c757600080fd5b505af11580156116db573d6000803e3d6000fd5b505050506040513d60208110156116f157600080fd5b81019080805190602001909291905050509050919050565b6000808373ffffffffffffffffffffffffffffffffffffffff1663693ec85e846040518263ffffffff167c01000000000000000000000000000000000000000000000000000000000281526004018080602001828103825283818151815260200191508051906020019080838360005b83811015611794578082015181840152602081019050611779565b50505050905090810190601f1680156117c15780820380516001836020036101000a031916815260200191505b50925050506040805180830381600087803b1580156117df57600080fd5b505af11580156117f3573d6000803e3d6000fd5b505050506040513d604081101561180957600080fd5b8101908080519060200190929190805190602001909291905050509150915092509290505600a165627a7a72305820dcbd9e6996d3d19928cd509f3d3b2225e5f2e3f2b7147f79b6f52289ba1b6cf70029";
    std::string tableTestAddress;
    std::string sender;
};

BOOST_FIXTURE_TEST_SUITE(precompiledTableTest, TableFactoryPrecompiledFixture)

BOOST_AUTO_TEST_CASE(createTableTest)
{
    deployTest();
    {
        creatTable(false, 1, 100, "t_test", "id", "item_name,item_id");
    }

    // createTable exist
    {
        creatTable(false,2, 101, "t_test", "id", "item_name,item_id", CODE_TABLE_NAME_ALREADY_EXIST);
    }

    // createTable too long tableName, key and filed
    std::string errorStr;
    for (int i = 0; i <= SYS_TABLE_VALUE_FIELD_MAX_LENGTH; i++)
    {
        errorStr += std::to_string(9);
    }
    {
        auto r1 =  creatTable(false,2, 102, errorStr, "id", "item_name,item_id");
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r2 = creatTable(false, 2, 103, "t_test", errorStr, "item_name,item_id");
        BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r3 = creatTable(false,2, 104, "t_test", "id", errorStr);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // createTable error key and filed
    std::string errorStr2 = "/test&";
    {
        auto r1 = creatTable(false,2, 105, errorStr2, "id", "item_name,item_id");
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r2 = creatTable(false,2, 106, "t_test", errorStr2, "item_name,item_id");
        BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r3 = creatTable(false,2, 107, "t_test", "id", errorStr2);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }
}

BOOST_AUTO_TEST_CASE(createKVTableTest)
{
    deployTest();
    {
        creatTable(true, 1, 100, "t_test", "id", "item_name,item_id");
    }

    // createTable exist
    {
        creatTable(true,2, 101, "t_test", "id", "item_name,item_id", CODE_TABLE_NAME_ALREADY_EXIST);
    }

    // createTable too long tableName, key and filed
    std::string errorStr;
    for (int i = 0; i <= SYS_TABLE_VALUE_FIELD_MAX_LENGTH; i++)
    {
        errorStr += std::to_string(9);
    }
    {
        auto r1 =  creatTable(true,2, 102, errorStr, "id", "item_name,item_id");
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r2 = creatTable(true, 2, 103, "t_test", errorStr, "item_name,item_id");
        BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r3 = creatTable(true,2, 104, "t_test", "id", errorStr);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // createTable error key and filed
    std::string errorStr2 = "/test&";
    {
        auto r1 = creatTable(true,2, 105, errorStr2, "id", "item_name,item_id");
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r2 = creatTable(true,2, 106, "t_test", errorStr2, "item_name,item_id");
        BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r3 = creatTable(true,2, 107, "t_test", "id", errorStr2);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }
}

BOOST_AUTO_TEST_CASE(openTableTest)
{
    deployTest();
    creatTable(false, 2, 100, "t_test", "id", "item_name,item_id");
    {
        nextBlock(3);
        auto result = openTable(false,  101, "t_poor");

        auto r1 = codec->encodeWithSig("Error(string)", std::string("/tables/t_poor does not exist"));
        BOOST_CHECK(result->status() == (int32_t)TransactionStatus::PrecompiledError);
        BOOST_CHECK(result->data().toBytes() == r1);

        auto result2 = openTable(false,  102, "t_test");
        Address addressOutAddress;
        codec->decode(result2->data(), addressOutAddress);
        BOOST_TEST(addressOutAddress.hex() == "0000000000000000000000000000000000010001");

        auto result3 = openTable(false,  103, "t_test");
        codec->decode(result3->data(), addressOutAddress);
        BOOST_TEST(addressOutAddress.hex() == "0000000000000000000000000000000000010002");
        commitBlock(3);
    }
}

BOOST_AUTO_TEST_CASE(openKVTableTest)
{
    deployTest();
    creatTable(true, 2, 100, "t_test", "id", "item_name,item_id");
    {
        nextBlock(3);
        auto result = openTable(true, 101, "t_poor");

        auto r1 = codec->encodeWithSig("Error(string)", std::string("/tables/t_poor does not exist"));
        BOOST_CHECK(result->status() == (int32_t)TransactionStatus::PrecompiledError);
        BOOST_CHECK(result->data().toBytes() == r1);

        auto result2 = openTable(true, 102, "t_test");
        Address addressOutAddress;
        codec->decode(result2->data(), addressOutAddress);
        BOOST_TEST(addressOutAddress.hex() == "0000000000000000000000000000000000010001");

        auto result3 = openTable(true, 103, "t_test");
        codec->decode(result3->data(), addressOutAddress);
        BOOST_TEST(addressOutAddress.hex() == "0000000000000000000000000000000000010002");
        commitBlock(3);
    }
}

BOOST_AUTO_TEST_CASE(selectTest)
{
    deployTest();

}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
