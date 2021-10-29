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
 */
/**
 * @brief : unitest for Wasm executor implementation
 * @author: catli
 * @date: 2021-10-19
 */

#include "../liquid/hello_world.h"
#include "../liquid/hello_world_caller.h"
#include "../liquid/transfer.h"
#include "../mock/MockTransactionalStorage.h"
#include "../mock/MockTxPool.h"
#include "Common.h"
#include "bcos-executor/TransactionExecutor.h"
#include "interfaces/crypto/CommonType.h"
#include "interfaces/crypto/CryptoSuite.h"
#include "interfaces/crypto/Hash.h"
#include "interfaces/executor/ExecutionMessage.h"
#include "interfaces/protocol/Transaction.h"
#include "libprotocol/protobuf/PBBlockHeader.h"
#include "libstorage/StateStorage.h"
#include "precompiled/PrecompiledCodec.h"
#include <bcos-framework/libexecutor/NativeExecutionMessage.h>
#include <bcos-framework/testutils/crypto/HashImpl.h>
#include <bcos-framework/testutils/crypto/SignatureImpl.h>
#include <bcos-framework/testutils/protocol/FakeBlockHeader.h>
#include <bcos-framework/testutils/protocol/FakeTransaction.h>
#include <unistd.h>
#include <boost/algorithm/hex.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <set>

using namespace std;
using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;

namespace bcos
{
namespace test
{
struct WasmExecutorFixture
{
    WasmExecutorFixture()
    {
        hashImpl = std::make_shared<Keccak256Hash>();
        assert(hashImpl);
        auto signatureImpl = std::make_shared<Secp256k1SignatureImpl>();
        assert(signatureImpl);
        cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);

        txpool = std::make_shared<MockTxPool>();
        backend = std::make_shared<MockTransactionalStorage>(hashImpl);
        auto executionResultFactory = std::make_shared<NativeExecutionMessageFactory>();

        executor = std::make_shared<TransactionExecutor>(
            txpool, nullptr, backend, executionResultFactory, hashImpl, true);

        keyPair = cryptoSuite->signatureImpl()->generateKeyPair();
        memcpy(keyPair->secretKey()->mutableData(),
            fromHexString("ff6f30856ad3bae00b1169808488502786a13e3c174d85682135ffd51310310e")
                ->data(),
            32);
        memcpy(keyPair->publicKey()->mutableData(),
            fromHexString(
                "ccd8de502ac45462767e649b462b5f4ca7eadd69c7e1f1b410bdf754359be29b1b88ffd79744"
                "03f56e250af52b25682014554f7b3297d6152401e85d426a06ae")
                ->data(),
            64);

        codec = std::make_unique<bcos::precompiled::PrecompiledCodec>(hashImpl, true);

        helloWorldBin.assign(hello_world_wasm, hello_world_wasm + hello_world_wasm_len);
        helloWorldBin = codec->encode(helloWorldBin);
        helloWorldAbi = codec->encode(string(
            R"([{"inputs":[{"internalType":"string","name":"name","type":"string"}],"type":"constructor"},{"conflictFields":[{"kind":0,"path":[],"read_only":false,"slot":0}],"constant":false,"inputs":[{"internalType":"string","name":"name","type":"string"}],"name":"set","outputs":[],"type":"function"},{"constant":true,"inputs":[],"name":"get","outputs":[{"internalType":"string","type":"string"}],"type":"function"}])"));

        helloWorldCallerBin.assign(
            hello_world_caller_wasm, hello_world_caller_wasm + hello_world_caller_wasm_len);
        helloWorldCallerBin = codec->encode(helloWorldCallerBin);
        helloWorldCallerAbi = codec->encode(string(
            R"([{"inputs":[{"internalType":"string","name":"addr","type":"string"}],"type":"constructor"},{"constant":false,"inputs":[{"internalType":"string","name":"name","type":"string"}],"name":"set","outputs":[],"type":"function"},{"constant":true,"inputs":[],"name":"get","outputs":[{"internalType":"string","type":"string"}],"type":"function"}])"));

        transferBin.assign(transfer_wasm, transfer_wasm + transfer_wasm_len);
        transferBin = codec->encode(transferBin);
        transferAbi = codec->encode(string(
            R"([{"inputs":[],"type":"constructor"},{"conflictFields":[{"kind":3,"path":[0],"read_only":false,"slot":0},{"kind":3,"path":[1],"read_only":false,"slot":0}],"constant":false,"inputs":[{"internalType":"string","name":"from","type":"string"},{"internalType":"string","name":"to","type":"string"},{"internalType":"uint32","name":"amount","type":"uint32"}],"name":"transfer","outputs":[{"internalType":"bool","type":"bool"}],"type":"function"},{"constant":true,"inputs":[{"internalType":"string","name":"name","type":"string"}],"name":"query","outputs":[{"internalType":"uint32","type":"uint32"}],"type":"function"}])"));
    }

    TransactionExecutor::Ptr executor;
    CryptoSuite::Ptr cryptoSuite;
    std::shared_ptr<MockTxPool> txpool;
    std::shared_ptr<MockTransactionalStorage> backend;
    std::shared_ptr<Keccak256Hash> hashImpl;

    KeyPairInterface::Ptr keyPair;
    int64_t gas = 3000000000;
    std::unique_ptr<bcos::precompiled::PrecompiledCodec> codec;

    bytes helloWorldBin;
    bytes helloWorldAbi;

    bytes helloWorldCallerBin;
    bytes helloWorldCallerAbi;

    bytes transferBin;
    bytes transferAbi;
};
BOOST_FIXTURE_TEST_SUITE(TestWasmExecutor, WasmExecutorFixture)

BOOST_AUTO_TEST_CASE(deployAndCall)
{
    bytes input = helloWorldBin;

    bytes constructorParam = codec->encode(string("alice"));
    constructorParam = codec->encode(constructorParam);
    input.insert(input.end(), constructorParam.begin(), constructorParam.end());

    string selfAddress = "/usr/alice/hello_world";
    bytes path = codec->encode(selfAddress);
    input.insert(input.end(), path.begin(), path.end());

    input.insert(input.end(), helloWorldAbi.begin(), helloWorldAbi.end());

    auto tx = fakeTransaction(cryptoSuite, keyPair, "", input, 101, 100001, "1", "1");
    auto sender = *toHexString(string_view((char*)tx->sender().data(), tx->sender().size()));

    auto hash = tx->hash();
    txpool->hash2Transaction.emplace(hash, tx);

    auto params = std::make_unique<NativeExecutionMessage>();
    params->setType(bcos::protocol::ExecutionMessage::TXHASH);
    params->setContextID(100);
    params->setSeq(1000);
    params->setDepth(0);
    params->setTo(selfAddress);
    params->setStaticCall(false);
    params->setGasAvailable(gas);
    params->setType(ExecutionMessage::TXHASH);
    params->setTransactionHash(hash);
    params->setCreate(true);

    NativeExecutionMessage paramsBak = *params;

    auto blockHeader = std::make_shared<bcos::protocol::PBBlockHeader>(cryptoSuite);
    blockHeader->setNumber(1);

    std::promise<void> nextPromise;
    executor->nextBlockHeader(blockHeader, [&](bcos::Error::Ptr&& error) {
        BOOST_CHECK(!error);
        nextPromise.set_value();
    });
    nextPromise.get_future().get();

    std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise;
    executor->executeTransaction(std::move(params),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise.set_value(std::move(result));
        });

    auto result = executePromise.get_future().get();
    BOOST_CHECK_EQUAL(result->status(), 0);
    BOOST_CHECK_EQUAL(result->origin(), sender);
    BOOST_CHECK_EQUAL(result->from(), paramsBak.to());
    BOOST_CHECK_EQUAL(result->to(), sender);

    BOOST_CHECK(result->message().empty());
    BOOST_CHECK(!result->newEVMContractAddress().empty());
    BOOST_CHECK_LT(result->gasAvailable(), gas);

    auto address = result->newEVMContractAddress();

    bcos::executor::TransactionExecutor::TwoPCParams commitParams;
    commitParams.number = 1;

    std::promise<void> preparePromise;
    executor->prepare(commitParams, [&](bcos::Error::Ptr&& error) {
        BOOST_CHECK(!error);
        preparePromise.set_value();
    });
    preparePromise.get_future().get();

    std::promise<void> commitPromise;
    executor->commit(commitParams, [&](bcos::Error::Ptr&& error) {
        BOOST_CHECK(!error);
        commitPromise.set_value();
    });
    commitPromise.get_future().get();
    auto tableName = std::string("/apps") + std::string(result->newEVMContractAddress());

    EXECUTOR_LOG(TRACE) << "Checking table: " << tableName;
    std::promise<Table> tablePromise;
    backend->asyncOpenTable(tableName, [&](Error::UniquePtr&& error, std::optional<Table>&& table) {
        BOOST_CHECK(!error);
        BOOST_CHECK(table);
        tablePromise.set_value(std::move(*table));
    });
    auto table = tablePromise.get_future().get();

    auto entry = table.getRow("code");
    BOOST_CHECK(entry);
    BOOST_CHECK_GT(entry->getField(STORAGE_VALUE).size(), 0);

    // start new block
    auto blockHeader2 = std::make_shared<bcos::protocol::PBBlockHeader>(cryptoSuite);
    blockHeader2->setNumber(2);

    std::promise<void> nextPromise2;
    executor->nextBlockHeader(std::move(blockHeader2), [&](bcos::Error::Ptr&& error) {
        BOOST_CHECK(!error);

        nextPromise2.set_value();
    });

    nextPromise2.get_future().get();

    // set "fisco bcos"
    bytes txInput;
    char inputBytes[] = "4ed3885e28666973636f2062636f73";
    boost::algorithm::unhex(
        &inputBytes[0], inputBytes + sizeof(inputBytes) - 1, std::back_inserter(txInput));
    auto params2 = std::make_unique<NativeExecutionMessage>();
    params2->setContextID(101);
    params2->setSeq(1000);
    params2->setDepth(0);
    params2->setFrom(std::string(sender));
    params2->setTo(std::string(address));
    params2->setOrigin(std::string(sender));
    params2->setStaticCall(false);
    params2->setGasAvailable(gas);
    params2->setData(std::move(txInput));
    params2->setType(NativeExecutionMessage::MESSAGE);

    std::promise<ExecutionMessage::UniquePtr> executePromise2;
    executor->executeTransaction(std::move(params2),
        [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise2.set_value(std::move(result));
        });
    auto result2 = executePromise2.get_future().get();

    BOOST_CHECK(result2);
    BOOST_CHECK_EQUAL(result2->status(), 0);
    BOOST_CHECK_EQUAL(result2->message(), "");
    BOOST_CHECK_EQUAL(result2->newEVMContractAddress(), "");
    BOOST_CHECK_LT(result2->gasAvailable(), gas);

    // read "fisco bcos"
    bytes queryBytes;
    char inputBytes2[] = "6d4ce63c";
    boost::algorithm::unhex(
        &inputBytes2[0], inputBytes2 + sizeof(inputBytes2) - 1, std::back_inserter(queryBytes));

    auto params3 = std::make_unique<NativeExecutionMessage>();
    params3->setContextID(102);
    params3->setSeq(1000);
    params3->setDepth(0);
    params3->setFrom(std::string(sender));
    params3->setTo(std::string(address));
    params3->setOrigin(std::string(sender));
    params3->setStaticCall(false);
    params3->setGasAvailable(gas);
    params3->setData(std::move(queryBytes));
    params3->setType(ExecutionMessage::MESSAGE);

    std::promise<ExecutionMessage::UniquePtr> executePromise3;
    executor->executeTransaction(std::move(params3),
        [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise3.set_value(std::move(result));
        });
    auto result3 = executePromise3.get_future().get();

    BOOST_CHECK(result3);
    BOOST_CHECK_EQUAL(result3->status(), 0);
    BOOST_CHECK_EQUAL(result3->message(), "");
    BOOST_CHECK_EQUAL(result3->newEVMContractAddress(), "");
    BOOST_CHECK_LT(result3->gasAvailable(), gas);

    std::string output;
    codec->decode(result3->data(), output);
    BOOST_CHECK_EQUAL(output, "fisco bcos");
}

BOOST_AUTO_TEST_CASE(externalCall)
{
    string aliceAddress = "/usr/alice/hello_world";
    string bobAddress = "/usr/bob/hello_world_caller";
    string sender;
    // --------------------------------
    // Create contract HelloWorld
    // --------------------------------
    {
        bytes input = helloWorldBin;

        bytes constructorParam = codec->encode(string("alice"));
        constructorParam = codec->encode(constructorParam);
        input.insert(input.end(), constructorParam.begin(), constructorParam.end());

        bytes path = codec->encode(aliceAddress);
        input.insert(input.end(), path.begin(), path.end());

        input.insert(input.end(), helloWorldAbi.begin(), helloWorldAbi.end());

        auto tx = fakeTransaction(cryptoSuite, keyPair, "", input, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));

        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);

        auto params = std::make_unique<NativeExecutionMessage>();
        params->setContextID(100);
        params->setSeq(1000);
        params->setDepth(0);
        params->setOrigin(std::string(sender));
        params->setFrom(std::string(sender));
        params->setTo(aliceAddress);
        params->setStaticCall(false);
        params->setGasAvailable(gas);
        params->setType(NativeExecutionMessage::TXHASH);
        params->setTransactionHash(hash);
        params->setCreate(true);

        auto blockHeader = std::make_shared<bcos::protocol::PBBlockHeader>(cryptoSuite);
        blockHeader->setNumber(1);

        std::promise<void> nextPromise;
        executor->nextBlockHeader(blockHeader, [&](bcos::Error::Ptr&& error) {
            BOOST_CHECK(!error);
            nextPromise.set_value();
        });
        nextPromise.get_future().get();

        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise;
        executor->executeTransaction(
            std::move(params), [&](bcos::Error::UniquePtr&& error,
                                   bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise.set_value(std::move(result));
            });

        auto result = executePromise.get_future().get();

        auto address = result->newEVMContractAddress();
        BOOST_CHECK_EQUAL(result->type(), NativeExecutionMessage::FINISHED);
        BOOST_CHECK_EQUAL(result->status(), 0);
        BOOST_CHECK_EQUAL(address, aliceAddress);
    }

    // --------------------------------
    // Create contract HelloWorldCaller
    // --------------------------------
    {
        bytes input = helloWorldCallerBin;

        bytes constructorParam = codec->encode(aliceAddress);
        constructorParam = codec->encode(constructorParam);
        input.insert(input.end(), constructorParam.begin(), constructorParam.end());

        bytes path = codec->encode(bobAddress);
        input.insert(input.end(), path.begin(), path.end());

        input.insert(input.end(), helloWorldCallerAbi.begin(), helloWorldCallerAbi.end());

        auto tx = fakeTransaction(cryptoSuite, keyPair, "", input, 102, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));

        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);

        auto params = std::make_unique<NativeExecutionMessage>();
        params->setContextID(200);
        params->setSeq(1002);
        params->setDepth(0);
        params->setOrigin(std::string(sender));
        params->setFrom(std::string(sender));
        params->setTo(bobAddress);
        params->setStaticCall(false);
        params->setGasAvailable(gas);
        params->setType(NativeExecutionMessage::TXHASH);
        params->setTransactionHash(hash);
        params->setCreate(true);

        auto blockHeader = std::make_shared<bcos::protocol::PBBlockHeader>(cryptoSuite);
        blockHeader->setNumber(2);

        std::promise<void> nextPromise;
        executor->nextBlockHeader(blockHeader, [&](bcos::Error::Ptr&& error) {
            BOOST_CHECK(!error);
            nextPromise.set_value();
        });
        nextPromise.get_future().get();

        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise;
        executor->executeTransaction(
            std::move(params), [&](bcos::Error::UniquePtr&& error,
                                   bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise.set_value(std::move(result));
            });

        auto result = executePromise.get_future().get();

        auto address = result->newEVMContractAddress();
        BOOST_CHECK_EQUAL(result->type(), NativeExecutionMessage::FINISHED);
        BOOST_CHECK_EQUAL(result->status(), 0);
        BOOST_CHECK_EQUAL(address, bobAddress);
    }

    // --------------------------------
    // HelloWorldCaller calls `set` of HelloWorld
    // --------------------------------
    {
        auto params = std::make_unique<NativeExecutionMessage>();
        params->setContextID(300);
        params->setSeq(1003);
        params->setDepth(0);
        params->setFrom(std::string(sender));
        params->setTo(std::string(bobAddress));
        params->setOrigin(std::string(sender));
        params->setStaticCall(false);
        params->setGasAvailable(gas);
        params->setCreate(false);
        params->setData(codec->encodeWithSig("set(string)", string("fisco bcos")));
        params->setType(NativeExecutionMessage::MESSAGE);

        auto blockHeader = std::make_shared<bcos::protocol::PBBlockHeader>(cryptoSuite);
        blockHeader->setNumber(2);

        std::promise<void> nextPromise;
        executor->nextBlockHeader(blockHeader, [&](bcos::Error::Ptr&& error) {
            BOOST_CHECK(!error);
            nextPromise.set_value();
        });
        nextPromise.get_future().get();

        std::promise<ExecutionMessage::UniquePtr> executePromise;
        executor->executeTransaction(std::move(params),
            [&](bcos::Error::UniquePtr&& error, NativeExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise.set_value(std::move(result));
            });
        auto result = executePromise.get_future().get();

        BOOST_CHECK(result);
        BOOST_CHECK_EQUAL(result->type(), ExecutionMessage::MESSAGE);
        BOOST_CHECK_EQUAL(result->data().size(), 15);
        BOOST_CHECK_EQUAL(result->contextID(), 300);
        BOOST_CHECK_EQUAL(result->seq(), 1003);
        BOOST_CHECK_EQUAL(result->create(), false);
        BOOST_CHECK_EQUAL(result->newEVMContractAddress(), "");
        BOOST_CHECK_EQUAL(result->origin(), std::string(sender));
        BOOST_CHECK_EQUAL(result->from(), std::string(bobAddress));
        BOOST_CHECK_EQUAL(result->to(), aliceAddress);
        BOOST_CHECK_LT(result->gasAvailable(), gas);

        result->setSeq(1004);
        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(
            std::move(result), [&](bcos::Error::UniquePtr&& error,
                                   bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        BOOST_CHECK(result2);
        BOOST_CHECK_EQUAL(result2->type(), ExecutionMessage::FINISHED);
        BOOST_CHECK_EQUAL(result2->data().size(), 0);
        BOOST_CHECK_EQUAL(result2->contextID(), 300);
        BOOST_CHECK_EQUAL(result2->seq(), 1004);
        BOOST_CHECK_EQUAL(result2->origin(), std::string(sender));
        BOOST_CHECK_EQUAL(result2->from(), aliceAddress);
        BOOST_CHECK_EQUAL(result2->to(), bobAddress);
        BOOST_CHECK_EQUAL(result2->create(), false);
        BOOST_CHECK_EQUAL(result2->status(), 0);
    }
}

BOOST_AUTO_TEST_CASE(performance)
{
    size_t count = 10 * 1000;

    bytes input = transferBin;
    input.push_back(0);

    string transferAddress = "/usr/alice/transfer";
    bytes path = codec->encode(transferAddress);
    input.insert(input.end(), path.begin(), path.end());

    input.insert(input.end(), transferAbi.begin(), transferAbi.end());

    auto tx = fakeTransaction(cryptoSuite, keyPair, "", input, 101, 100001, "1", "1");
    auto sender = boost::algorithm::hex_lower(std::string(tx->sender()));

    auto hash = tx->hash();
    txpool->hash2Transaction.emplace(hash, tx);

    auto params = std::make_unique<NativeExecutionMessage>();
    params->setContextID(99);
    params->setSeq(1000);
    params->setDepth(0);
    params->setOrigin(std::string(sender));
    params->setFrom(std::string(sender));
    params->setTo(transferAddress);
    params->setStaticCall(false);
    params->setGasAvailable(gas);
    params->setType(NativeExecutionMessage::TXHASH);
    params->setTransactionHash(hash);
    params->setCreate(true);

    auto blockHeader = std::make_shared<bcos::protocol::PBBlockHeader>(cryptoSuite);
    blockHeader->setNumber(1);

    std::promise<void> nextPromise;
    executor->nextBlockHeader(blockHeader, [&](bcos::Error::Ptr&& error) {
        BOOST_CHECK(!error);
        nextPromise.set_value();
    });
    nextPromise.get_future().get();

    // --------------------------------
    // Create contract transfer
    // --------------------------------
    std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise;
    executor->executeTransaction(std::move(params),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise.set_value(std::move(result));
        });

    auto result = executePromise.get_future().get();

    auto address = result->newEVMContractAddress();

    std::vector<ExecutionMessage::UniquePtr> requests;
    requests.reserve(count);
    // Transfer
    for (size_t i = 0; i < count; ++i)
    {
        auto params = std::make_unique<NativeExecutionMessage>();
        params->setContextID(i);
        params->setSeq(6000);
        params->setDepth(0);
        params->setFrom(std::string(sender));
        params->setTo(std::string(address));
        params->setOrigin(std::string(sender));
        params->setStaticCall(false);
        params->setGasAvailable(gas);
        params->setCreate(false);

        std::string from = "alice";
        std::string to = "bob";
        uint32_t amount = 1;
        params->setData(codec->encodeWithSig("transfer(string,string,uint32)", from, to, amount));
        params->setType(NativeExecutionMessage::MESSAGE);

        requests.emplace_back(std::move(params));
    }

    auto now = std::chrono::system_clock::now();

    for (auto& it : requests)
    {
        std::optional<ExecutionMessage::UniquePtr> output;
        executor->executeTransaction(std::move(it),
            [&output](bcos::Error::UniquePtr&& error, NativeExecutionMessage::UniquePtr&& result) {
                if (error)
                {
                    std::cout << "Error!" << boost::diagnostic_information(*error);
                }
                // BOOST_CHECK(!error);
                output = std::move(result);
            });
        auto& result = *output;
        if (result->status() != 0)
        {
            std::cout << "Error: " << result->status() << std::endl;
        }
    }

    std::cout << "Execute elapsed: "
              << (std::chrono::system_clock::now() - now).count() / 1000 / 1000 << std::endl;

    {
        bytes queryBytes = codec->encodeWithSig("query(string)", string("alice"));

        auto params = std::make_unique<NativeExecutionMessage>();
        params->setContextID(102);
        params->setSeq(1000);
        params->setDepth(0);
        params->setFrom(std::string(sender));
        params->setTo(std::string(address));
        params->setOrigin(std::string(sender));
        params->setStaticCall(false);
        params->setGasAvailable(gas);
        params->setData(std::move(queryBytes));
        params->setType(ExecutionMessage::MESSAGE);

        std::promise<ExecutionMessage::UniquePtr> executePromise;
        executor->executeTransaction(std::move(params),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise.set_value(std::move(result));
            });
        auto result = executePromise.get_future().get();

        BOOST_CHECK(result);
        BOOST_CHECK_EQUAL(result->status(), 0);
        BOOST_CHECK_EQUAL(result->message(), "");
        BOOST_CHECK_EQUAL(result->newEVMContractAddress(), "");
        BOOST_CHECK_LT(result->gasAvailable(), gas);

        uint32_t dept;
        codec->decode(result->data(), dept);
        BOOST_CHECK_EQUAL(dept, numeric_limits<uint32_t>::max() - count);
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
