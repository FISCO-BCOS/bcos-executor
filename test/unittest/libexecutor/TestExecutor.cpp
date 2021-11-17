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

#include "../mock/MockTransactionalStorage.h"
#include "../mock/MockTxPool.h"
#include "Common.h"
#include "bcos-executor/LRUStorage.h"
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
#include <tbb/task_group.h>
#include <boost/algorithm/hex.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/thread/latch.hpp>
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
        auto executionResultFactory = std::make_shared<NativeExecutionMessageFactory>();

        auto lruStorage = std::make_shared<bcos::executor::LRUStorage>(backend);

        executor = std::make_shared<TransactionExecutor>(
            txpool, lruStorage, backend, executionResultFactory, hashImpl, false, false);

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

    auto hash = tx->hash();
    txpool->hash2Transaction.emplace(hash, tx);

    auto params = std::make_unique<NativeExecutionMessage>();
    params->setType(bcos::protocol::ExecutionMessage::TXHASH);
    params->setContextID(100);
    params->setSeq(1000);
    params->setDepth(0);

    // The contract address
    h256 addressCreate("ff6f30856ad3bae00b1169808488502786a13e3c174d85682135ffd51310310e");
    std::string addressString = addressCreate.hex().substr(0, 40);
    params->setTo(std::move(addressString));

    params->setStaticCall(false);
    params->setGasAvailable(gas);
    // params->setData(input);
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

    bcos::executor::TransactionExecutor::TwoPCParams commitParams{};
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
    BOOST_CHECK_GT(entry->getField(0).size(), 0);

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
    boost::algorithm::hex_lower(
        result3->data().begin(), result3->data().end(), std::back_inserter(output));
    BOOST_CHECK_EQUAL(output,
        "00000000000000000000000000000000000000000000000000000000000000200000000000000000000"
        "000000000000000000000000000000000000000000005666973636f0000000000000000000000000000"
        "00000000000000000000000000");
}

BOOST_AUTO_TEST_CASE(externalCall)
{
    // Solidity source code from test_external_call.sol, using remix
    // 0.6.10+commit.00c0fcaf

    std::string ABin =
        "608060405234801561001057600080fd5b5061037f806100206000396000f3fe60806040523480156100105760"
        "0080fd5b506004361061002b5760003560e01c80635b975a7314610030575b600080fd5b61005c600480360360"
        "2081101561004657600080fd5b8101908080359060200190929190505050610072565b60405180828152602001"
        "91505060405180910390f35b600081604051610081906101c7565b808281526020019150506040518091039060"
        "00f0801580156100a7573d6000803e3d6000fd5b506000806101000a81548173ffffffffffffffffffffffffff"
        "ffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff1602179055507fd8e189e965"
        "f1ff506594c5c65110ea4132cee975b58710da78ea19bc094414ae826040518082815260200191505060405180"
        "910390a16000809054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffff"
        "ffffffffffffffffffffffffffff16633fa4f2456040518163ffffffff1660e01b815260040160206040518083"
        "038186803b15801561018557600080fd5b505afa158015610199573d6000803e3d6000fd5b505050506040513d"
        "60208110156101af57600080fd5b81019080805190602001909291905050509050919050565b610175806101d5"
        "8339019056fe608060405234801561001057600080fd5b50604051610175380380610175833981810160405260"
        "2081101561003357600080fd5b8101908080519060200190929190505050806000819055507fdc509bfccbee28"
        "6f248e0904323788ad0c0e04e04de65c04b482b056acb1a0658160405180828152602001915050604051809103"
        "90a15060e4806100916000396000f3fe6080604052348015600f57600080fd5b506004361060325760003560e0"
        "1c80633fa4f245146037578063a16fe09b146053575b600080fd5b603d605b565b604051808281526020019150"
        "5060405180910390f35b60596064565b005b60008054905090565b6000808154600101919050819055507f052f"
        "6b9dfac9e4e1257cb5b806b7673421c54730f663c8ab02561743bb23622d600054604051808281526020019150"
        "5060405180910390a156fea264697066735822122006eea3bbe24f3d859a9cb90efc318f26898aeb4dffb31cac"
        "e105776a6c272f8464736f6c634300060a0033a2646970667358221220b441da8ba792a40e444d0ed767a4417e"
        "944c55578d1c8d0ca4ad4ec050e05a9364736f6c634300060a0033";

    std::string BBin =
        "608060405234801561001057600080fd5b50604051610175380380610175833981810160405260208110156100"
        "3357600080fd5b8101908080519060200190929190505050806000819055507fdc509bfccbee286f248e090432"
        "3788ad0c0e04e04de65c04b482b056acb1a065816040518082815260200191505060405180910390a15060e480"
        "6100916000396000f3fe6080604052348015600f57600080fd5b506004361060325760003560e01c80633fa4f2"
        "45146037578063a16fe09b146053575b600080fd5b603d605b565b604051808281526020019150506040518091"
        "0390f35b60596064565b005b60008054905090565b6000808154600101919050819055507f052f6b9dfac9e4e1"
        "257cb5b806b7673421c54730f663c8ab02561743bb23622d600054604051808281526020019150506040518091"
        "0390a156fea264697066735822122006eea3bbe24f3d859a9cb90efc318f26898aeb4dffb31cace105776a6c27"
        "2f8464736f6c634300060a0033";

    bytes input;
    boost::algorithm::unhex(ABin, std::back_inserter(input));
    auto tx = fakeTransaction(cryptoSuite, keyPair, "", input, 101, 100001, "1", "1");
    auto sender = boost::algorithm::hex_lower(std::string(tx->sender()));

    auto hash = tx->hash();
    txpool->hash2Transaction.emplace(hash, tx);

    auto params = std::make_unique<NativeExecutionMessage>();
    params->setContextID(100);
    params->setSeq(1000);
    params->setDepth(0);

    params->setOrigin(std::string(sender));
    params->setFrom(std::string(sender));

    // The contract address
    h256 addressCreate("ff6f30856ad3bae00b1169808488502786a13e3c174d85682135ffd51310310e");
    std::string addressString = addressCreate.hex().substr(0, 40);
    params->setTo(std::move(addressString));

    params->setStaticCall(false);
    params->setGasAvailable(gas);
    params->setData(input);
    params->setType(NativeExecutionMessage::TXHASH);
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
    BOOST_CHECK_EQUAL(result->type(), NativeExecutionMessage::FINISHED);
    BOOST_CHECK_EQUAL(result->status(), 0);
    BOOST_CHECK_GT(address.size(), 0);
    BOOST_CHECK(result->keyLocks().empty());

    // --------------------------------
    // Call A createAndCallB(int256)
    // --------------------------------
    auto params2 = std::make_unique<NativeExecutionMessage>();
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
    params2->setType(NativeExecutionMessage::MESSAGE);

    std::promise<ExecutionMessage::UniquePtr> executePromise2;
    executor->executeTransaction(std::move(params2),
        [&](bcos::Error::UniquePtr&& error, NativeExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise2.set_value(std::move(result));
        });
    auto result2 = executePromise2.get_future().get();

    BOOST_CHECK(result2);
    BOOST_CHECK_EQUAL(result2->type(), ExecutionMessage::MESSAGE);
    BOOST_CHECK(result2->data().size() > 0);
    BOOST_CHECK_EQUAL(result2->contextID(), 101);
    BOOST_CHECK_EQUAL(result2->seq(), 1001);
    BOOST_CHECK_EQUAL(result2->create(), true);
    BOOST_CHECK_EQUAL(result2->newEVMContractAddress(), "");
    BOOST_CHECK_EQUAL(result2->origin(), std::string(sender));
    BOOST_CHECK_EQUAL(result2->from(), std::string(address));
    BOOST_CHECK(result2->to().empty());
    BOOST_CHECK_LT(result2->gasAvailable(), gas);
    BOOST_CHECK_EQUAL(result2->keyLocks().size(), 1);
    BOOST_CHECK_EQUAL(result2->keyLocks()[0], "code");

    // --------------------------------
    // Message 1: Create contract B, set new seq 1002
    // A -> B
    // --------------------------------
    result2->setSeq(1002);

    h256 addressCreate2(
        "ee6f30856ad3bae00b1169808488502786a13e3c174d85682135ffd51310310e");  // ee6f30856ad3bae00b1169808488502786a13e3c
    std::string addressString2 = addressCreate2.hex().substr(0, 40);
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
    BOOST_CHECK(result3->logEntries().size() == 0);
    BOOST_CHECK(result3->keyLocks().empty());

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
    BOOST_CHECK_EQUAL(result4->to(), boost::algorithm::to_lower_copy(std::string(addressString2)));
    BOOST_CHECK_EQUAL(result4->keyLocks().size(), 1);
    BOOST_CHECK_EQUAL(toHex(result4->keyLocks()[0]), h256(0).hex());  // first member

    // Request message without status
    // BOOST_CHECK_EQUAL(result4->status(), 0);
    BOOST_CHECK(result4->message().empty());
    BOOST_CHECK(result4->newEVMContractAddress().empty());
    BOOST_CHECK_GT(result4->keyLocks().size(), 0);

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
    BOOST_CHECK_EQUAL(
        result5->from(), boost::algorithm::to_lower_copy(std::string(addressString2)));
    BOOST_CHECK_EQUAL(result5->to(), std::string(address));
    BOOST_CHECK_EQUAL(result5->status(), 0);
    BOOST_CHECK(result5->message().empty());
    BOOST_CHECK_EQUAL(result5->keyLocks().size(), 0);

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
    BOOST_CHECK(result6->logEntries().size() == 1);
    BOOST_CHECK_EQUAL(result6->keyLocks().size(), 0);

    executor->getHash(1, [&](bcos::Error::UniquePtr&& error, crypto::HashType&& hash) {
        BOOST_CHECK(!error);
        BOOST_CHECK_NE(hash.hex(), h256().hex());
    });

    // commit the state
    bcos::executor::ParallelTransactionExecutorInterface::TwoPCParams commitParams;
    commitParams.number = 1;

    executor->prepare(commitParams, [](bcos::Error::Ptr error) { BOOST_CHECK(!error); });
    executor->commit(commitParams, [](bcos::Error::Ptr error) { BOOST_CHECK(!error); });

    // execute a call request
    auto callParam = std::make_unique<NativeExecutionMessage>();
    callParam->setType(executor::NativeExecutionMessage::MESSAGE);
    callParam->setContextID(500);
    callParam->setSeq(7778);
    callParam->setDepth(0);
    callParam->setFrom(std::string(sender));
    callParam->setTo(boost::algorithm::to_lower_copy(std::string(addressString2)));
    callParam->setData(codec->encodeWithSig("value()"));
    callParam->setOrigin(std::string(sender));
    callParam->setStaticCall(true);
    callParam->setGasAvailable(gas);
    callParam->setCreate(false);

    bcos::protocol::ExecutionMessage::UniquePtr callResult;
    executor->call(std::move(callParam),
        [&](bcos::Error::UniquePtr error, bcos::protocol::ExecutionMessage::UniquePtr response) {
            BOOST_CHECK(!error);
            callResult = std::move(response);
        });

    BOOST_CHECK_EQUAL(callResult->type(), protocol::ExecutionMessage::FINISHED);
    BOOST_CHECK_EQUAL(callResult->status(), 0);

    auto expectResult = codec->encode(s256(1000));
    BOOST_CHECK(callResult->data().toBytes() == expectResult);

    // commit the state, and call
    // bcos::executor::TransactionExecutor::TwoPCParams commitParams;
    // commitParams.number = 1;
    // executor->prepare(commitParams, [&](bcos::Error::Ptr error) { BOOST_CHECK(!error); });
    // executor->commit(commitParams, [&](bcos::Error::Ptr error) { BOOST_CHECK(!error); });

    auto callParam2 = std::make_unique<NativeExecutionMessage>();
    callParam2->setType(executor::NativeExecutionMessage::MESSAGE);
    callParam2->setContextID(501);
    callParam2->setSeq(7779);
    callParam2->setDepth(0);
    callParam2->setFrom(std::string(sender));
    callParam2->setTo(boost::algorithm::to_lower_copy(std::string(addressString2)));
    callParam2->setData(codec->encodeWithSig("value()"));
    callParam2->setOrigin(std::string(sender));
    callParam2->setStaticCall(true);
    callParam2->setGasAvailable(gas);
    callParam2->setCreate(false);

    bcos::protocol::ExecutionMessage::UniquePtr callResult2;
    executor->call(std::move(callParam2),
        [&](bcos::Error::UniquePtr error, bcos::protocol::ExecutionMessage::UniquePtr response) {
            BOOST_CHECK(!error);
            callResult2 = std::move(response);
        });

    BOOST_CHECK_EQUAL(callResult2->type(), protocol::ExecutionMessage::FINISHED);
    BOOST_CHECK_EQUAL(callResult2->status(), 0);

    auto expectResult2 = codec->encode(s256(1000));
    BOOST_CHECK(callResult2->data().toBytes() == expectResult);
}

BOOST_AUTO_TEST_CASE(performance)
{
    size_t count = 10 * 100;

    bcos::crypto::HashType hash;
    for (size_t blockNumber = 1; blockNumber < 10; ++blockNumber)
    {
        std::string bin =
            "608060405234801561001057600080fd5b506105db806100206000396000f3006080604052600436106100"
            "6257"
            "6000357c0100000000000000000000000000000000000000000000000000000000900463ffffffff168063"
            "35ee"
            "5f87146100675780638a42ebe9146100e45780639b80b05014610157578063fad42f8714610210575b6000"
            "80fd"
            "5b34801561007357600080fd5b506100ce6004803603810190808035906020019082018035906020019080"
            "8060"
            "1f016020809104026020016040519081016040528093929190818152602001838380828437820191505050"
            "5050"
            "5091929192905050506102c9565b6040518082815260200191505060405180910390f35b3480156100f057"
            "6000"
            "80fd5b50610155600480360381019080803590602001908201803590602001908080601f01602080910402"
            "6020"
            "01604051908101604052809392919081815260200183838082843782019150505050505091929192908035"
            "9060"
            "20019092919050505061033d565b005b34801561016357600080fd5b5061020e6004803603810190808035"
            "9060"
            "2001908201803590602001908080601f016020809104026020016040519081016040528093929190818152"
            "6020"
            "018383808284378201915050505050509192919290803590602001908201803590602001908080601f0160"
            "2080"
            "91040260200160405190810160405280939291908181526020018383808284378201915050505050509192"
            "9192"
            "90803590602001909291905050506103b1565b005b34801561021c57600080fd5b506102c7600480360381"
            "0190"
            "80803590602001908201803590602001908080601f01602080910402602001604051908101604052809392"
            "9190"
            "81815260200183838082843782019150505050505091929192908035906020019082018035906020019080"
            "8060"
            "1f016020809104026020016040519081016040528093929190818152602001838380828437820191505050"
            "5050"
            "509192919290803590602001909291905050506104a8565b005b6000808260405180828051906020019080"
            "8383"
            "5b60208310151561030257805182526020820191506020810190506020830392506102dd565b6001836020"
            "0361"
            "01000a03801982511681845116808217855250505050505090500191505090815260200160405180910390"
            "2054"
            "9050919050565b806000836040518082805190602001908083835b60208310151561037657805182526020"
            "8201"
            "9150602081019050602083039250610351565b6001836020036101000a0380198251168184511680821785"
            "5250"
            "50505050509050019150509081526020016040518091039020819055505050565b80600084604051808280"
            "5190"
            "602001908083835b6020831015156103ea57805182526020820191506020810190506020830392506103c5"
            "565b"
            "6001836020036101000a038019825116818451168082178552505050505050905001915050908152602001"
            "6040"
            "51809103902060008282540392505081905550806000836040518082805190602001908083835b60208310"
            "1515"
            "610463578051825260208201915060208101905060208303925061043e565b6001836020036101000a0380"
            "1982"
            "51168184511680821785525050505050509050019150509081526020016040518091039020600082825401"
            "9250"
            "5081905550505050565b806000846040518082805190602001908083835b6020831015156104e157805182"
            "5260"
            "20820191506020810190506020830392506104bc565b6001836020036101000a0380198251168184511680"
            "8217"
            "85525050505050509050019150509081526020016040518091039020600082825403925050819055508060"
            "0083"
            "6040518082805190602001908083835b60208310151561055a578051825260208201915060208101905060"
            "2083"
            "039250610535565b6001836020036101000a03801982511681845116808217855250505050505090500191"
            "5050"
            "908152602001604051809103902060008282540192505081905550606481111515156105aa57600080fd5b"
            "5050"
            "505600a165627a7a723058205669c1a68cebcef35822edcec77a15792da5c32a8aa127803290253b3d5f62"
            "7200"
            "29";

        bytes input;
        boost::algorithm::unhex(bin, std::back_inserter(input));
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

        // The contract address
        std::string addressSeed = "address" + boost::lexical_cast<std::string>(blockNumber);
        h256 addressCreate(hashImpl->hash(addressSeed));
        // h256 addressCreate("ff6f30856ad3bae00b1169808488502786a13e3c174d85682135ffd51310310e");
        std::string addressString = addressCreate.hex().substr(0, 40);
        // toChecksumAddress(addressString, hashImpl);
        params->setTo(std::move(addressString));

        params->setStaticCall(false);
        params->setGasAvailable(gas);
        params->setData(input);
        params->setType(NativeExecutionMessage::TXHASH);
        params->setTransactionHash(hash);
        params->setCreate(true);

        NativeExecutionMessage paramsBak = *params;

        auto blockHeader = std::make_shared<bcos::protocol::PBBlockHeader>(cryptoSuite);
        blockHeader->setNumber(blockNumber);

        std::promise<void> nextPromise;
        executor->nextBlockHeader(blockHeader, [&](bcos::Error::Ptr&& error) {
            BOOST_CHECK(!error);
            nextPromise.set_value();
        });
        nextPromise.get_future().get();

        // --------------------------------
        // Create contract ParallelOk
        // --------------------------------
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise;
        executor->executeTransaction(
            std::move(params), [&](bcos::Error::UniquePtr&& error,
                                   bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise.set_value(std::move(result));
            });

        auto result = executePromise.get_future().get();

        auto address = result->newEVMContractAddress();

        // Set user
        for (size_t i = 0; i < count; ++i)
        {
            params = std::make_unique<NativeExecutionMessage>();
            params->setContextID(i);
            params->setSeq(5000);
            params->setDepth(0);
            params->setFrom(std::string(sender));
            params->setTo(std::string(address));
            params->setOrigin(std::string(sender));
            params->setStaticCall(false);
            params->setGasAvailable(gas);
            params->setCreate(false);

            std::string user = "user" + boost::lexical_cast<std::string>(i);
            bcos::u256 value(1000000);
            params->setData(codec->encodeWithSig("set(string,uint256)", user, value));
            params->setType(NativeExecutionMessage::MESSAGE);

            std::promise<ExecutionMessage::UniquePtr> executePromise2;
            executor->executeTransaction(std::move(params),
                [&](bcos::Error::UniquePtr&& error, NativeExecutionMessage::UniquePtr&& result) {
                    if (error)
                    {
                        std::cout << "Error!" << boost::diagnostic_information(*error);
                    }
                    executePromise2.set_value(std::move(result));
                });
            auto result2 = executePromise2.get_future().get();
            // BOOST_CHECK_EQUAL(result->status(), 0);
        }

        std::vector<ExecutionMessage::UniquePtr> requests;
        requests.reserve(count);
        // Transfer
        for (size_t i = 0; i < count; ++i)
        {
            params = std::make_unique<NativeExecutionMessage>();
            params->setContextID(i);
            params->setSeq(6000);
            params->setDepth(0);
            params->setFrom(std::string(sender));
            params->setTo(std::string(address));
            params->setOrigin(std::string(sender));
            params->setStaticCall(false);
            params->setGasAvailable(gas);
            params->setCreate(false);

            std::string from = "user" + boost::lexical_cast<std::string>(i);
            std::string to = "user" + boost::lexical_cast<std::string>(count - 1);
            bcos::u256 value(10);
            params->setData(
                codec->encodeWithSig("transfer(string,string,uint256)", from, to, value));
            params->setType(NativeExecutionMessage::MESSAGE);

            requests.emplace_back(std::move(params));
        }

        auto now = std::chrono::system_clock::now();

        for (auto& it : requests)
        {
            std::optional<ExecutionMessage::UniquePtr> output;
            executor->executeTransaction(
                std::move(it), [&output](bcos::Error::UniquePtr&& error,
                                   NativeExecutionMessage::UniquePtr&& result) {
                    if (error)
                    {
                        std::cout << "Error!" << boost::diagnostic_information(*error);
                    }
                    // BOOST_CHECK(!error);
                    output = std::move(result);
                });
            auto& transResult = *output;
            if (transResult->status() != 0)
            {
                std::cout << "Error: " << transResult->status() << std::endl;
            }
        }

        std::cout << "Execute elapsed: "
                  << (std::chrono::system_clock::now() - now).count() / 1000 / 1000 << std::endl;

        now = std::chrono::system_clock::now();
        // Check the result
        for (size_t i = 0; i < count; ++i)
        {
            params = std::make_unique<NativeExecutionMessage>();
            params->setContextID(i);
            params->setSeq(7000);
            params->setDepth(0);
            params->setFrom(std::string(sender));
            params->setTo(std::string(address));
            params->setOrigin(std::string(sender));
            params->setStaticCall(false);
            params->setGasAvailable(gas);
            params->setCreate(false);

            std::string account = "user" + boost::lexical_cast<std::string>(i);
            params->setData(codec->encodeWithSig("balanceOf(string)", account));
            params->setType(NativeExecutionMessage::MESSAGE);

            std::optional<ExecutionMessage::UniquePtr> output;
            executor->executeTransaction(
                std::move(params), [&output](bcos::Error::UniquePtr&& error,
                                       NativeExecutionMessage::UniquePtr&& result) {
                    if (error)
                    {
                        std::cout << "Error!" << boost::diagnostic_information(*error);
                    }
                    // BOOST_CHECK(!error);
                    output = std::move(result);
                });
            auto& balanceResult = *output;

            bcos::u256 value(0);
            codec->decode(balanceResult->data(), value);

            if (i < count - 1)
            {
                BOOST_CHECK_EQUAL(value, u256(1000000 - 10));
            }
            else
            {
                BOOST_CHECK_EQUAL(value, u256(1000000 + 10 * (count - 1)));
            }
        }

        std::cout << "Check elapsed: "
                  << (std::chrono::system_clock::now() - now).count() / 1000 / 1000 << std::endl;

        executor->getHash(
            blockNumber, [&hash](bcos::Error::UniquePtr error, crypto::HashType resultHash) {
                BOOST_CHECK(!error);
                BOOST_CHECK_NE(resultHash, h256());

                if (hash == h256())
                {
                    hash = resultHash;
                }
                else
                {
                    hash = resultHash;
                }
            });
    }
}

BOOST_AUTO_TEST_CASE(multiDeploy)
{
    tbb::task_group group;

    size_t count = 100;
    std::vector<NativeExecutionMessage::UniquePtr> paramsList;

    for (size_t i = 0; i < count; ++i)
    {
        auto helloworld = string(helloBin);
        bytes input;
        boost::algorithm::unhex(helloworld, std::back_inserter(input));
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", input, 100 + i, 100001, "1", "1");

        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params = std::make_unique<NativeExecutionMessage>();
        params->setType(bcos::protocol::ExecutionMessage::TXHASH);
        params->setContextID(100 + i);
        params->setSeq(1000);
        params->setDepth(0);
        auto sender = *toHexString(string_view((char*)tx->sender().data(), tx->sender().size()));

        auto addressCreate =
            cryptoSuite->hashImpl()->hash("i am a address" + boost::lexical_cast<std::string>(i));
        std::string addressString = addressCreate.hex().substr(0, 40);
        params->setTo(std::move(addressString));

        params->setStaticCall(false);
        params->setGasAvailable(gas);
        // params->setData(input);
        params->setType(ExecutionMessage::TXHASH);
        params->setTransactionHash(hash);
        params->setCreate(true);

        paramsList.emplace_back(std::move(params));
    }

    auto blockHeader = std::make_shared<bcos::protocol::PBBlockHeader>(cryptoSuite);
    blockHeader->setNumber(1);

    std::promise<void> nextPromise;
    executor->nextBlockHeader(blockHeader, [&](bcos::Error::Ptr&& error) {
        BOOST_CHECK(!error);
        nextPromise.set_value();
    });
    nextPromise.get_future().get();

    boost::latch latch(paramsList.size());

    std::vector<std::tuple<bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr>>
        responses(count);
    for (size_t i = 0; i < paramsList.size(); ++i)
    {
        group.run([&responses, executor = executor, &paramsList, index = i, &latch]() {
            executor->executeTransaction(std::move(std::move(paramsList[index])),
                [&](bcos::Error::UniquePtr error,
                    bcos::protocol::ExecutionMessage::UniquePtr result) {
                    responses[index] = std::make_tuple(std::move(error), std::move(result));
                    latch.count_down();
                });
        });
    }

    latch.wait();
    group.wait();

    for (auto& it : responses)
    {
        auto& [error, result] = it;

        BOOST_CHECK(!error);
        if (error)
        {
            std::cout << boost::diagnostic_information(*error) << std::endl;
        }

        BOOST_CHECK_EQUAL(result->status(), 0);

        BOOST_CHECK(result->message().empty());
        BOOST_CHECK(!result->newEVMContractAddress().empty());
        BOOST_CHECK_LT(result->gasAvailable(), gas);
    }
}

BOOST_AUTO_TEST_CASE(keyLock) {}

BOOST_AUTO_TEST_CASE(deployErrorCode)
{
    // an infinity-loop constructor
    std::string errorBin =
        "608060405234801561001057600080fd5b505b60011561006a576040518060400160405280600381526020017f"
        "313233000000000000000000000000000000000000000000000000000000000081525060009080519060200190"
        "61006492919061006f565b50610012565b610114565b8280546001816001161561010002031660029004906000"
        "52602060002090601f016020900481019282601f106100b057805160ff19168380011785556100de565b828001"
        "600101855582156100de579182015b828111156100dd5782518255916020019190600101906100c2565b5b5090"
        "506100eb91906100ef565b5090565b61011191905b8082111561010d5760008160009055506001016100f5565b"
        "5090565b90565b6101f8806101236000396000f3fe608060405234801561001057600080fd5b50600436106100"
        "365760003560e01c806344733ae11461003b5780638e397a0314610059575b600080fd5b610043610063565b60"
        "40516100509190610140565b60405180910390f35b610061610105565b005b6060600080546001816001161561"
        "01000203166002900480601f016020809104026020016040519081016040528092919081815260200182805460"
        "0181600116156101000203166002900480156100fb5780601f106100d057610100808354040283529160200191"
        "6100fb565b820191906000526020600020905b8154815290600101906020018083116100de57829003601f1682"
        "01915b5050505050905090565b565b600061011282610162565b61011c818561016d565b935061012c81856020"
        "860161017e565b610135816101b1565b840191505092915050565b600060208201905081810360008301526101"
        "5a8184610107565b905092915050565b600081519050919050565b600082825260208201905092915050565b60"
        "005b8381101561019c578082015181840152602081019050610181565b838111156101ab576000848401525b50"
        "505050565b6000601f19601f830116905091905056fea2646970667358221220e4e19dff46d31f82111f9261d8"
        "687c52312c9221962991e27bbddc409dfbd7c564736f6c634300060a0033";
    bytes input;
    boost::algorithm::unhex(errorBin, std::back_inserter(input));
    auto tx = fakeTransaction(cryptoSuite, keyPair, "", input, 101, 100001, "1", "1");
    auto sender = boost::algorithm::hex_lower(std::string(tx->sender()));
    h256 addressCreate("ff6f30856ad3bae00b1169808488502786a13e3c174d85682135ffd51310310e");
    std::string addressString = addressCreate.hex().substr(0, 40);

    auto hash = tx->hash();
    txpool->hash2Transaction.emplace(hash, tx);

    auto params = std::make_unique<NativeExecutionMessage>();
    params->setContextID(99);
    params->setSeq(1000);
    params->setDepth(0);

    params->setOrigin(sender);
    params->setFrom(sender);

    // toChecksumAddress(addressString, hashImpl);
    params->setTo(addressString);
    params->setStaticCall(false);
    params->setGasAvailable(gas);
    params->setData(input);
    params->setType(NativeExecutionMessage::TXHASH);
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
    // --------------------------------
    // Create contract
    // --------------------------------

    std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise;
    executor->executeTransaction(std::move(params),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise.set_value(std::move(result));
        });

    auto result = executePromise.get_future().get();
    BOOST_CHECK(result);
    BOOST_CHECK_EQUAL(result->type(), ExecutionMessage::REVERT);
    BOOST_CHECK_EQUAL(result->status(), (int32_t)TransactionStatus::OutOfGas);
    BOOST_CHECK_EQUAL(result->contextID(), 99);
    BOOST_CHECK_EQUAL(result->seq(), 1000);
    BOOST_CHECK_EQUAL(result->create(), false);
    BOOST_CHECK_EQUAL(result->newEVMContractAddress(), "");
    BOOST_CHECK_EQUAL(result->origin(), sender);
    BOOST_CHECK_EQUAL(result->from(), addressString);
    BOOST_CHECK(result->to() == sender);

    bcos::executor::TransactionExecutor::TwoPCParams commitParams{};
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
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
