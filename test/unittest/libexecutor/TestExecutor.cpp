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
 * @brief : unitest for executor implement
 * @author: ancelmo
 * @date: 2021-09-14
 */

#include "../mock/MockExecutionMessage.h"
#include "../mock/MockTransactionalStorage.h"
#include "../mock/MockTxPool.h"
#include "ChecksumAddress.h"
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
#include <bcos-framework/testutils/crypto/HashImpl.h>
#include <bcos-framework/testutils/crypto/SignatureImpl.h>
#include <bcos-framework/testutils/protocol/FakeBlockHeader.h>
#include <bcos-framework/testutils/protocol/FakeTransaction.h>
#include <boost/algorithm/hex.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
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
struct TransactionExecutorFixture
{
    TransactionExecutorFixture()
    {
        hashImpl = std::make_shared<Keccak256Hash>();
        assert(hashImpl);
        auto signatureImpl = std::make_shared<Secp256k1SignatureImpl>();
        assert(signatureImpl);
        cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);

        txpool = std::make_shared<MockTxPool>();
        backend = std::make_shared<MockTransactionalStorage>(hashImpl);
        auto executionResultFactory = std::make_shared<MockExecutionMessageFactory>();

        executor = std::make_shared<TransactionExecutor>(
            txpool, backend, executionResultFactory, hashImpl, false);

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

        codec = std::make_unique<bcos::precompiled::PrecompiledCodec>(hashImpl, false);
    }

    TransactionExecutor::Ptr executor;
    CryptoSuite::Ptr cryptoSuite;
    std::shared_ptr<MockTxPool> txpool;
    std::shared_ptr<MockTransactionalStorage> backend;
    std::shared_ptr<Keccak256Hash> hashImpl;

    KeyPairInterface::Ptr keyPair;
    int64_t gas = 3000000;
    std::unique_ptr<bcos::precompiled::PrecompiledCodec> codec;

    string helloBin =
        "60806040526040805190810160405280600181526020017f3100000000000000000000000000000000000000"
        "0000000000000000000000008152506001908051906020019061004f9291906100ae565b5034801561005c5760"
        "0080fd5b506040805190810160405280600d81526020017f48656c6c6f2c20576f726c64210000000000000000"
        "0000000000000000000000815250600090805190602001906100a89291906100ae565b50610153565b82805460"
        "0181600116156101000203166002900490600052602060002090601f016020900481019282601f106100ef5780"
        "5160ff191683800117855561011d565b8280016001018555821561011d579182015b8281111561011c57825182"
        "5591602001919060010190610101565b5b50905061012a919061012e565b5090565b61015091905b8082111561"
        "014c576000816000905550600101610134565b5090565b90565b6104ac806101626000396000f3006080604052"
        "60043610610057576000357c0100000000000000000000000000000000000000000000000000000000900463ff"
        "ffffff1680634ed3885e1461005c57806354fd4d50146100c55780636d4ce63c14610155575b600080fd5b3480"
        "1561006857600080fd5b506100c3600480360381019080803590602001908201803590602001908080601f0160"
        "208091040260200160405190810160405280939291908181526020018383808284378201915050505050509192"
        "9192905050506101e5565b005b3480156100d157600080fd5b506100da61029b565b6040518080602001828103"
        "825283818151815260200191508051906020019080838360005b8381101561011a578082015181840152602081"
        "0190506100ff565b50505050905090810190601f1680156101475780820380516001836020036101000a031916"
        "815260200191505b509250505060405180910390f35b34801561016157600080fd5b5061016a610339565b6040"
        "518080602001828103825283818151815260200191508051906020019080838360005b838110156101aa578082"
        "01518184015260208101905061018f565b50505050905090810190601f1680156101d757808203805160018360"
        "20036101000a031916815260200191505b509250505060405180910390f35b80600090805190602001906101fb"
        "9291906103db565b507f93a093529f9c8a0c300db4c55fcd27c068c4f5e0e8410bc288c7e76f3d71083e816040"
        "518080602001828103825283818151815260200191508051906020019080838360005b8381101561025e578082"
        "015181840152602081019050610243565b50505050905090810190601f16801561028b57808203805160018360"
        "20036101000a031916815260200191505b509250505060405180910390a150565b600180546001816001161561"
        "01000203166002900480601f016020809104026020016040519081016040528092919081815260200182805460"
        "0181600116156101000203166002900480156103315780601f1061030657610100808354040283529160200191"
        "610331565b820191906000526020600020905b81548152906001019060200180831161031457829003601f1682"
        "01915b505050505081565b606060008054600181600116156101000203166002900480601f0160208091040260"
        "200160405190810160405280929190818152602001828054600181600116156101000203166002900480156103"
        "d15780601f106103a6576101008083540402835291602001916103d1565b820191906000526020600020905b81"
        "54815290600101906020018083116103b457829003601f168201915b5050505050905090565b82805460018160"
        "0116156101000203166002900490600052602060002090601f016020900481019282601f1061041c57805160ff"
        "191683800117855561044a565b8280016001018555821561044a579182015b8281111561044957825182559160"
        "200191906001019061042e565b5b509050610457919061045b565b5090565b61047d91905b8082111561047957"
        "6000816000905550600101610461565b5090565b905600a165627a7a723058204736027ad6b97d7cd2685379ac"
        "b35b386dcb18799934be8283f1e08cd1f0c6ec0029";
};
BOOST_FIXTURE_TEST_SUITE(TestTransactionExecutor, TransactionExecutorFixture)

BOOST_AUTO_TEST_CASE(deployAndCall)
{
    auto helloworld = string(helloBin);

    bytes input;
    boost::algorithm::unhex(helloworld, std::back_inserter(input));
    auto tx = fakeTransaction(cryptoSuite, keyPair, "", input, 101, 100001, "1", "1");
    auto sender = *toHexString(string_view((char*)tx->sender().data(), tx->sender().size()));
    toChecksumAddress(sender, hashImpl);

    auto hash = tx->hash();
    txpool->hash2Transaction.emplace(hash, tx);

    auto params = std::make_unique<MockExecutionMessage>();
    params->setContextID(100);
    params->setSeq(1000);
    params->setDepth(0);

    params->setOrigin(std::string(sender));
    params->setFrom(std::string(sender));

    // The contract address
    h256 addressCreate("ff6f30856ad3bae00b1169808488502786a13e3c174d85682135ffd51310310e");
    std::string addressString = addressCreate.hex().substr(0, 40);
    toChecksumAddress(addressString, hashImpl);
    params->setTo(std::move(addressString));

    params->setStaticCall(false);
    params->setGasAvailable(gas);
    params->setData(input);
    params->setType(MockExecutionMessage::TXHASH);
    params->setTransactionHash(hash);
    params->setCreate(true);

    MockExecutionMessage paramsBak = *params;

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

    BOOST_CHECK_EQUAL(result->origin(), paramsBak.origin());
    BOOST_CHECK_EQUAL(result->from(), paramsBak.to());
    BOOST_CHECK_EQUAL(result->to(), paramsBak.from());

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
    auto tableName = std::string("/apps/") +
                     std::string(result->newEVMContractAddress());  // TODO: ensure the contract
                                                                    // address is hex

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
    char inputBytes[] =
        "4ed3885e0000000000000000000000000000000000000000000000000000000000000020000000000000000000"
        "0000000000000000000000000000000000000000000005666973636f0000000000000000000000000000000000"
        "00000000000000000000";
    boost::algorithm::unhex(
        &inputBytes[0], inputBytes + sizeof(inputBytes) - 1, std::back_inserter(txInput));
    auto params2 = std::make_unique<MockExecutionMessage>();
    params2->setContextID(101);
    params2->setSeq(1000);
    params2->setDepth(0);
    params2->setFrom(std::string(sender));
    params2->setTo(std::string(address));
    params2->setOrigin(std::string(sender));
    params2->setStaticCall(false);
    params2->setGasAvailable(gas);
    params2->setData(std::move(txInput));
    params2->setType(MockExecutionMessage::MESSAGE);

    std::promise<ExecutionMessage::UniquePtr> executePromise2;
    executor->executeTransaction(std::move(params2),
        [&](bcos::Error::UniquePtr&& error, MockExecutionMessage::UniquePtr&& result) {
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

    auto params3 = std::make_unique<MockExecutionMessage>();
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
    boost::algorithm::hex_lower(
        result3->data().begin(), result3->data().end(), std::back_inserter(output));
    BOOST_CHECK_EQUAL(output,
        "00000000000000000000000000000000000000000000000000000000000000200000000000000000000"
        "000000000000000000000000000000000000000000005666973636f0000000000000000000000000000"
        "00000000000000000000000000");
}

BOOST_AUTO_TEST_CASE(externalCall)
{
    // Solidity source code from test_external_call.sol, using solc
    // 0.6.6+commit.6c089d02.Emscripten.clang

    std::string ABin =
        "608060405234801561001057600080fd5b50610314806100206000396000f30060806040526004361061004157"
        "6000357c0100000000000000000000000000000000000000000000000000000000900463ffffffff1680635b97"
        "5a7314610046575b600080fd5b34801561005257600080fd5b5061007160048036038101908080359060200190"
        "929190505050610087565b6040518082815260200191505060405180910390f35b6000816100926101bf565b80"
        "828152602001915050604051809103906000f0801580156100b8573d6000803e3d6000fd5b506000806101000a"
        "81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffff"
        "ffffffffff1602179055506000809054906101000a900473ffffffffffffffffffffffffffffffffffffffff16"
        "73ffffffffffffffffffffffffffffffffffffffff16633fa4f2456040518163ffffffff167c01000000000000"
        "00000000000000000000000000000000000000000000028152600401602060405180830381600087803b158015"
        "61017d57600080fd5b505af1158015610191573d6000803e3d6000fd5b505050506040513d60208110156101a7"
        "57600080fd5b81019080805190602001909291905050509050919050565b604051610119806101d08339019056"
        "00608060405234801561001057600080fd5b506040516020806101198339810180604052810190808051906020"
        "0190929190505050806000819055505060d0806100496000396000f3006080604052600436106049576000357c"
        "0100000000000000000000000000000000000000000000000000000000900463ffffffff1680633fa4f2451460"
        "4e578063a16fe09b146076575b600080fd5b348015605957600080fd5b506060608a565b604051808281526020"
        "0191505060405180910390f35b348015608157600080fd5b5060886093565b005b60008054905090565b600080"
        "8154600101919050819055505600a165627a7a72305820ef453a630f3810f42d0b9b9745396ea928d8ad0f762b"
        "158200a699e92876ff840029a165627a7a72305820e53e699223986ca2c9bf2d53f11d594168d858f7ea67c169"
        "cadb44950f7cc8db0029";

    std::string BBin =
        "608060405234801561001057600080fd5b50604051610105380380610105833981810160405260208110156100"
        "3357600080fd5b8101908080519060200190929190505050806000819055505060ab8061005a6000396000f3fe"
        "6080604052348015600f57600080fd5b506004361060325760003560e01c80633fa4f245146037578063a16fe0"
        "9b146053575b600080fd5b603d605b565b6040518082815260200191505060405180910390f35b60596064565b"
        "005b60008054905090565b60008081546001019190508190555056fea2646970667358221220214f0037b653f3"
        "ca36b8e77f9859af35cce186e03186594eb454877aa283f20e64736f6c63430006060033";

    bytes input;
    boost::algorithm::unhex(ABin, std::back_inserter(input));
    auto tx = fakeTransaction(cryptoSuite, keyPair, "", input, 101, 100001, "1", "1");
    auto sender = boost::algorithm::hex_lower(std::string(tx->sender()));
    toChecksumAddress(sender, hashImpl);

    auto hash = tx->hash();
    txpool->hash2Transaction.emplace(hash, tx);

    auto params = std::make_unique<MockExecutionMessage>();
    params->setContextID(100);
    params->setSeq(1000);
    params->setDepth(0);

    params->setOrigin(std::string(sender));
    params->setFrom(std::string(sender));

    // The contract address
    h256 addressCreate("ff6f30856ad3bae00b1169808488502786a13e3c174d85682135ffd51310310e");
    std::string addressString = addressCreate.hex().substr(0, 40);
    toChecksumAddress(addressString, hashImpl);
    params->setTo(std::move(addressString));

    params->setStaticCall(false);
    params->setGasAvailable(gas);
    params->setData(input);
    params->setType(MockExecutionMessage::TXHASH);
    params->setTransactionHash(hash);
    params->setCreate(true);

    MockExecutionMessage paramsBak = *params;

    auto blockHeader = std::make_shared<bcos::protocol::PBBlockHeader>(cryptoSuite);
    blockHeader->setNumber(1);

    std::promise<void> nextPromise;
    executor->nextBlockHeader(blockHeader, [&](bcos::Error::Ptr&& error) {
        BOOST_CHECK(!error);
        nextPromise.set_value();
    });
    nextPromise.get_future().get();

    // --------------------------------
    // Create contract A
    // --------------------------------
    std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise;
    executor->executeTransaction(std::move(params),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise.set_value(std::move(result));
        });

    auto result = executePromise.get_future().get();

    auto address = result->newEVMContractAddress();
    BOOST_CHECK_EQUAL(result->type(), MockExecutionMessage::FINISHED);
    BOOST_CHECK_EQUAL(result->status(), 0);
    BOOST_CHECK_GT(address.size(), 0);

    // --------------------------------
    // Call A createAndCallB(int256)
    // --------------------------------
    auto params2 = std::make_unique<MockExecutionMessage>();
    params2->setContextID(101);
    params2->setSeq(1001);
    params2->setDepth(0);
    params2->setFrom(std::string(sender));
    params2->setTo(std::string(address));
    params2->setOrigin(std::string(sender));
    params2->setStaticCall(false);
    params2->setGasAvailable(gas);
    params2->setCreate(false);

    bcos::u256 value(1000);
    params2->setData(codec->encodeWithSig("createAndCallB(int256)", value));
    params2->setType(MockExecutionMessage::MESSAGE);

    std::promise<ExecutionMessage::UniquePtr> executePromise2;
    executor->executeTransaction(std::move(params2),
        [&](bcos::Error::UniquePtr&& error, MockExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise2.set_value(std::move(result));
        });
    auto result2 = executePromise2.get_future().get();

    BOOST_CHECK(result2);
    BOOST_CHECK_EQUAL(result2->type(), ExecutionMessage::MESSAGE);
    BOOST_CHECK_EQUAL(result2->data().size(), 313);
    BOOST_CHECK_EQUAL(result2->contextID(), 101);
    BOOST_CHECK_EQUAL(result2->seq(), 1001);
    BOOST_CHECK_EQUAL(result2->create(), true);
    BOOST_CHECK_EQUAL(result2->newEVMContractAddress(), "");
    BOOST_CHECK_EQUAL(result2->origin(), std::string(sender));
    BOOST_CHECK_EQUAL(result2->from(), std::string(address));
    BOOST_CHECK(result2->to().empty());
    BOOST_CHECK_LT(result2->gasAvailable(), gas);

    // --------------------------------
    // Message 1: Create contract B, set new seq 1002
    // A -> B
    // --------------------------------
    result2->setSeq(1002);

    h256 addressCreate2(
        "ee6f30856ad3bae00b1169808488502786a13e3c174d85682135ffd51310310e");  // ee6f30856ad3bae00b1169808488502786a13e3c
    std::string addressString2 = addressCreate2.hex().substr(0, 40);
    toChecksumAddress(addressString2, hashImpl);
    result2->setTo(addressString2);

    std::promise<ExecutionMessage::UniquePtr> executePromise3;
    executor->executeTransaction(std::move(result2),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise3.set_value(std::move(result));
        });
    auto result3 = executePromise3.get_future().get();

    BOOST_CHECK(result3);
    BOOST_CHECK_EQUAL(result3->type(), ExecutionMessage::FINISHED);
    BOOST_CHECK_EQUAL(result3->data().size(), 0);
    BOOST_CHECK_EQUAL(result3->contextID(), 101);
    BOOST_CHECK_EQUAL(result3->seq(), 1002);
    BOOST_CHECK_EQUAL(result3->origin(), std::string(sender));
    BOOST_CHECK_EQUAL(result3->from(), addressString2);
    BOOST_CHECK_EQUAL(result3->to(), std::string(address));
    BOOST_CHECK_EQUAL(result3->newEVMContractAddress(), addressString2);
    BOOST_CHECK_EQUAL(result3->create(), false);
    BOOST_CHECK_EQUAL(result3->status(), 0);

    // --------------------------------
    // Message 2: Create contract B success return, set previous seq 1001
    // B -> A
    // --------------------------------
    result3->setSeq(1001);
    std::promise<ExecutionMessage::UniquePtr> executePromise4;
    executor->executeTransaction(std::move(result3),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise4.set_value(std::move(result));
        });
    auto result4 = executePromise4.get_future().get();

    BOOST_CHECK(result4);
    BOOST_CHECK_EQUAL(result4->type(), ExecutionMessage::MESSAGE);
    BOOST_CHECK_GT(result4->data().size(), 0);
    auto param = codec->encodeWithSig("value()");
    BOOST_CHECK(result4->data().toBytes() == param);
    BOOST_CHECK_EQUAL(result4->contextID(), 101);
    BOOST_CHECK_EQUAL(result4->seq(), 1001);
    BOOST_CHECK_EQUAL(result4->from(), std::string(address));
    BOOST_CHECK_EQUAL(result4->to(), std::string(addressString2));

    // Request message without status
    // BOOST_CHECK_EQUAL(result4->status(), 0);
    BOOST_CHECK(result4->message().empty());
    BOOST_CHECK(result4->newEVMContractAddress().empty());

    // --------------------------------
    // Message 3: A call B's value(), set new seq 1003
    // A -> B
    // --------------------------------
    result4->setSeq(1003);
    std::promise<ExecutionMessage::UniquePtr> executePromise5;
    executor->executeTransaction(std::move(result4),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise5.set_value(std::move(result));
        });
    auto result5 = executePromise5.get_future().get();

    BOOST_CHECK(result5);
    BOOST_CHECK_EQUAL(result5->type(), ExecutionMessage::FINISHED);
    BOOST_CHECK_GT(result5->data().size(), 0);
    param = codec->encode(s256(1000));
    BOOST_CHECK(result5->data().toBytes() == param);
    BOOST_CHECK_EQUAL(result5->contextID(), 101);
    BOOST_CHECK_EQUAL(result5->seq(), 1003);
    BOOST_CHECK_EQUAL(result5->from(), std::string(addressString2));
    BOOST_CHECK_EQUAL(result5->to(), std::string(address));
    BOOST_CHECK_EQUAL(result5->status(), 0);
    BOOST_CHECK(result5->message().empty());

    // --------------------------------
    // Message 4: A call B's success return, set previous seq 1001
    // B -> A
    // --------------------------------
    result5->setSeq(1001);
    std::promise<ExecutionMessage::UniquePtr> executePromise6;
    executor->executeTransaction(std::move(result5),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise6.set_value(std::move(result));
        });
    auto result6 = executePromise6.get_future().get();

    BOOST_CHECK(result6);
    BOOST_CHECK_EQUAL(result6->type(), ExecutionMessage::FINISHED);
    BOOST_CHECK_GT(result6->data().size(), 0);
    BOOST_CHECK(result6->data().toBytes() == param);
    BOOST_CHECK_EQUAL(result6->contextID(), 101);
    BOOST_CHECK_EQUAL(result6->seq(), 1001);
    BOOST_CHECK_EQUAL(result6->from(), std::string(address));
    BOOST_CHECK_EQUAL(result6->to(), std::string(sender));
    BOOST_CHECK_EQUAL(result6->origin(), std::string(sender));
    BOOST_CHECK_EQUAL(result6->status(), 0);
    BOOST_CHECK(result6->message().empty());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
