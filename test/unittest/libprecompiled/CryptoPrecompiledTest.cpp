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
 * @file CryptoPrecompiledTest.cpp
 * @author: kyonRay
 * @date 2021-07-05
 */

#include "precompiled/CryptoPrecompiled.h"
#include "libprecompiled/PreCompiledFixture.h"
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-crypto/signature/sm2/SM2Crypto.h>
#include <bcos-crypto/signature/sm2/SM2KeyPair.h>
#include <bcos-framework/testutils/TestPromptFixture.h>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::ledger;
using namespace bcos::crypto;
using namespace bcos::codec;

namespace bcos::test
{
class CryptoPrecompiledFixture : public PrecompiledFixture
{
public:
    CryptoPrecompiledFixture()
    {
        codec = std::make_shared<PrecompiledCodec>(hashImpl, false);
        setIsWasm(false);
        cryptoAddress = Address("0x420f853b49838bd3e9466c85a4cc3428c960dde2").hex();
    }

    virtual ~CryptoPrecompiledFixture() {}

    void deployTest()
    {
        bytes input;
        boost::algorithm::unhex(cryptoBin, std::back_inserter(input));
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
        params->setTo(cryptoAddress);
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
        BOOST_CHECK_EQUAL(result->newEVMContractAddress(), cryptoAddress);
        BOOST_CHECK_EQUAL(result->origin(), sender);
        BOOST_CHECK_EQUAL(result->from(), cryptoAddress);
        BOOST_CHECK(result->to() == sender);
        BOOST_CHECK_LT(result->gasAvailable(), gas);

        // --------------------------------
        // Create contract twice to avoid address used in wasm
        // --------------------------------

        paramsBak.setSeq(1001);
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(std::make_unique<decltype(paramsBak)>(paramsBak),
            [&](bcos::Error::UniquePtr&& error,
                bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });

        auto result2 = executePromise2.get_future().get();
        BOOST_CHECK(result2);
        BOOST_CHECK_EQUAL(result2->type(), ExecutionMessage::REVERT);
        BOOST_CHECK_EQUAL(
            result2->status(), (int32_t)TransactionStatus::ContractAddressAlreadyUsed);

        BOOST_CHECK_EQUAL(result2->contextID(), 99);
        commitBlock(1);
    }

    std::string sender;
    std::string cryptoAddress;
    std::string cryptoBin =
        "608060405234801561001057600080fd5b5061100a6000806101000a81548173ffffffffffffffffffffffffff"
        "ffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff1602179055506106b2806100"
        "626000396000f300608060405260043610610057576000357c0100000000000000000000000000000000000000"
        "000000000000000000900463ffffffff1680634cf2a67a1461005c578063eb90f45914610156578063fb34363c"
        "146101db575b600080fd5b34801561006857600080fd5b50610109600480360381019080803590602001908201"
        "803590602001908080601f01602080910402602001604051908101604052809392919081815260200183838082"
        "84378201915050505050509192919290803590602001908201803590602001908080601f016020809104026020"
        "016040519081016040528093929190818152602001838380828437820191505050505050919291929050505061"
        "0260565b60405180831515151581526020018273ffffffffffffffffffffffffffffffffffffffff1673ffffff"
        "ffffffffffffffffffffffffffffffffff1681526020019250505060405180910390f35b348015610162576000"
        "80fd5b506101bd600480360381019080803590602001908201803590602001908080601f016020809104026020"
        "016040519081016040528093929190818152602001838380828437820191505050505050919291929050505061"
        "0414565b60405180826000191660001916815260200191505060405180910390f35b3480156101e757600080fd"
        "5b50610242600480360381019080803590602001908201803590602001908080601f0160208091040260200160"
        "40519081016040528093929190818152602001838380828437820191505050505050919291929050505061054d"
        "565b60405180826000191660001916815260200191505060405180910390f35b6000806000809054906101000a"
        "900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff"
        "16634cf2a67a85856040518363ffffffff167c0100000000000000000000000000000000000000000000000000"
        "000000028152600401808060200180602001838103835285818151815260200191508051906020019080838360"
        "005b838110156103115780820151818401526020810190506102f6565b50505050905090810190601f16801561"
        "033e5780820380516001836020036101000a031916815260200191505b50838103825284818151815260200191"
        "508051906020019080838360005b8381101561037757808201518184015260208101905061035c565b50505050"
        "905090810190601f1680156103a45780820380516001836020036101000a031916815260200191505b50945050"
        "5050506040805180830381600087803b1580156103c457600080fd5b505af11580156103d8573d6000803e3d60"
        "00fd5b505050506040513d60408110156103ee57600080fd5b8101908080519060200190929190805190602001"
        "90929190505050915091509250929050565b60008060009054906101000a900473ffffffffffffffffffffffff"
        "ffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1663eb90f459836040518263ffffff"
        "ff167c010000000000000000000000000000000000000000000000000000000002815260040180806020018281"
        "03825283818151815260200191508051906020019080838360005b838110156104bf5780820151818401526020"
        "810190506104a4565b50505050905090810190601f1680156104ec5780820380516001836020036101000a0319"
        "16815260200191505b5092505050602060405180830381600087803b15801561050b57600080fd5b505af11580"
        "1561051f573d6000803e3d6000fd5b505050506040513d602081101561053557600080fd5b8101908080519060"
        "2001909291905050509050919050565b60008060009054906101000a900473ffffffffffffffffffffffffffff"
        "ffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1663fb34363c836040518263ffffffff16"
        "7c0100000000000000000000000000000000000000000000000000000000028152600401808060200182810382"
        "5283818151815260200191508051906020019080838360005b838110156105f857808201518184015260208101"
        "90506105dd565b50505050905090810190601f1680156106255780820380516001836020036101000a03191681"
        "5260200191505b5092505050602060405180830381600087803b15801561064457600080fd5b505af115801561"
        "0658573d6000803e3d6000fd5b505050506040513d602081101561066e57600080fd5b81019080805190602001"
        "9092919050505090509190505600a165627a7a72305820bfa24956af9f415a1cb9e19608cca1d4c6f72e0fffb6"
        "946ffeda715b32d2f0c70029";
};

BOOST_FIXTURE_TEST_SUITE(precompiledCryptoTest, CryptoPrecompiledFixture)

BOOST_AUTO_TEST_CASE(testSM3AndKeccak256)
{
    deployTest();

    // sm3
    {
        nextBlock(2);
        std::string stringData = "abcd";
        bytesConstRef dataRef(stringData);
        bytes encodedData = codec->encodeWithSig("sm3(bytes)", dataRef.toBytes());

        auto tx = fakeTransaction(cryptoSuite, keyPair, "", encodedData, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto txHash = tx->hash();
        txpool->hash2Transaction.emplace(txHash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(txHash);
        params2->setContextID(100);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(cryptoAddress);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(encodedData));
        params2->setType(NativeExecutionMessage::TXHASH);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        bytes out = result2->data().toBytes();
        string32 decodedHash;
        codec->decode(bytesConstRef(&out), decodedHash);
        HashType hash =
            HashType("82ec580fe6d36ae4f81cae3c73f4a5b3b5a09c943172dc9053c69fd8e18dca1e");
        std::cout << "== testHash-sm3: decodedHash: " << codec::fromString32(decodedHash).hex()
                  << std::endl;
        std::cout << "== testHash-sm3: hash:" << hash.hex() << std::endl;
        BOOST_CHECK(hash == codec::fromString32(decodedHash));

        commitBlock(2);
    }
    // keccak256Hash
    {
        nextBlock(3);
        std::string stringData = "abcd";
        bytesConstRef dataRef(stringData);
        bytes encodedData = codec->encodeWithSig("keccak256Hash(bytes)", dataRef.toBytes());

        auto tx = fakeTransaction(cryptoSuite, keyPair, "", encodedData, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto txHash = tx->hash();
        txpool->hash2Transaction.emplace(txHash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(txHash);
        params2->setContextID(101);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(cryptoAddress);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(encodedData));
        params2->setType(NativeExecutionMessage::TXHASH);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        bytes out = result2->data().toBytes();
        string32 decodedHash;
        codec->decode(bytesConstRef(&out), decodedHash);
        HashType hash =
            HashType("48bed44d1bcd124a28c27f343a817e5f5243190d3c52bf347daf876de1dbbf77");
        std::cout << "== testHash-keccak256Hash: decodedHash: "
                  << codec::fromString32(decodedHash).hex() << std::endl;
        std::cout << "== testHash-keccak256Hash: hash:" << hash.hex() << std::endl;
        BOOST_CHECK(hash == codec::fromString32(decodedHash));
        commitBlock(3);
    }
}

BOOST_AUTO_TEST_CASE(testSM2Verify)
{
    deployTest();

    // case Verify success
    h256 fixedSec1("bcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd");
    auto sec1 = std::make_shared<KeyImpl>(fixedSec1.asBytes());
    auto keyFactory = std::make_shared<KeyFactoryImpl>();
    auto secCreated = keyFactory->createKey(fixedSec1.asBytes());

    auto keyPair = std::make_shared<SM2KeyPair>(sec1);
    HashType hash = HashType("82ec580fe6d36ae4f81cae3c73f4a5b3b5a09c943172dc9053c69fd8e18dca1e");
    auto signature = sm2Sign(keyPair, hash, true);
    h256 mismatchHash = h256("c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470");
    // verify the signature
    bytes encodedData = codec->encodeWithSig("sm2Verify(bytes,bytes)", hash.asBytes(), *signature);

    // verify
    {
        nextBlock(2);

        auto tx = fakeTransaction(cryptoSuite, keyPair, "", encodedData, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto txHash = tx->hash();
        txpool->hash2Transaction.emplace(txHash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(txHash);
        params2->setContextID(100);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(cryptoAddress);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(encodedData));
        params2->setType(NativeExecutionMessage::TXHASH);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        bytes out = result2->data().toBytes();
        bool verifySucc;
        Address accountAddress;
        codec->decode(ref(out), verifySucc, accountAddress);
        std::cout << "== testSM2Verify-normalCase, verifySucc: " << verifySucc << std::endl;
        std::cout << "== testSM2Verify-normalCase, accountAddress: " << accountAddress.hex()
                  << std::endl;
        std::cout << "== realAccountAddress:" << keyPair->address(smHashImpl).hex() << std::endl;
        BOOST_CHECK(verifySucc == true);
        BOOST_CHECK(accountAddress.hex() == keyPair->address(smHashImpl).hex());
        commitBlock(2);
    }

    // mismatch
    {
        nextBlock(3);

        encodedData =
            codec->encodeWithSig("sm2Verify(bytes,bytes)", mismatchHash.asBytes(), *signature);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", encodedData, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto txHash = tx->hash();
        txpool->hash2Transaction.emplace(txHash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(txHash);
        params2->setContextID(101);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(cryptoAddress);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(encodedData));
        params2->setType(NativeExecutionMessage::TXHASH);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        bytes out = result2->data().toBytes();
        bool verifySucc;
        Address accountAddress;
        codec->decode(ref(out), verifySucc, accountAddress);
        std::cout << "== testSM2Verify-mismatchHashCase, verifySucc: " << verifySucc << std::endl;
        std::cout << "== testSM2Verify-mismatchHashCase, accountAddress: " << accountAddress.hex()
                  << std::endl;
        std::cout << "== realAccountAddress:" << keyPair->address(smHashImpl).hex() << std::endl;
        BOOST_CHECK(verifySucc == false);
        BOOST_CHECK(accountAddress.hex() == Address().hex());
        commitBlock(3);
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
