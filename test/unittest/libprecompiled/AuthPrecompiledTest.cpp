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
 * @file AuthPrecompiledTest.cpp
 * @author: kyonRay
 * @date 2021-11-15
 */

#include "libprecompiled/PreCompiledFixture.h"
#include "precompiled/extension/ContractAuthPrecompiled.h"
#include <bcos-framework/interfaces/executor/PrecompiledTypeDef.h>
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
class PermissionPrecompiledFixture : public PrecompiledFixture
{
public:
    PermissionPrecompiledFixture()
    {
        codec = std::make_shared<PrecompiledCodec>(hashImpl, false);
        setIsWasm(false, true);
        helloAddress = Address("0x1234654b49838bd3e9466c85a4cc3428c9601234").hex();
    }

    virtual ~PermissionPrecompiledFixture() {}

    void deployHello()
    {
        bytes input;
        boost::algorithm::unhex(helloBin, std::back_inserter(input));
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
        params->setTo(helloAddress);
        params->setStaticCall(false);
        params->setGasAvailable(gas);
        params->setData(input);
        params->setType(NativeExecutionMessage::TXHASH);
        params->setTransactionHash(hash);
        params->setCreate(true);

        NativeExecutionMessage paramsBak = *params;
        nextBlock(2);
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
        BOOST_CHECK_EQUAL(result->newEVMContractAddress(), helloAddress);
        BOOST_CHECK_EQUAL(result->origin(), sender);
        BOOST_CHECK_EQUAL(result->from(), helloAddress);
        BOOST_CHECK(result->to() == sender);
        BOOST_CHECK_LT(result->gasAvailable(), gas);

        commitBlock(2);
    }

    ExecutionMessage::UniquePtr helloGet(protocol::BlockNumber _number, int _contextId,
        int _errorCode = 0, Address _address = Address())
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("get()");
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        if (_address != Address())
        {
            tx->forceSender(_address.asBytes());
        }
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        // force cover write
        txpool->hash2Transaction[hash] = tx;
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(_contextId);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(helloAddress);
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

        if (_errorCode != 0)
        {
            BOOST_CHECK(result2->data().toBytes() == codec->encode(s256(_errorCode)));
        }
        commitBlock(_number);

        return result2;
    };

    ExecutionMessage::UniquePtr helloSet(protocol::BlockNumber _number, int _contextId,
        const std::string& _value, int _errorCode = 0, Address _address = Address())
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("set(string)", _value);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        if (_address != Address())
        {
            tx->forceSender(_address.asBytes());
        }
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        // force cover write
        txpool->hash2Transaction[hash] = tx;
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(_contextId);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(helloAddress);
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

        if (_errorCode != 0)
        {
            BOOST_CHECK(result2->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);

        return result2;
    };

    ExecutionMessage::UniquePtr authSetMethodType(protocol::BlockNumber _number, int _contextId,
        Address const& _path, std::string const& helloMethod, precompiled::AuthType _type,
        int _errorCode = 0)
    {
        nextBlock(_number);
        bytes func = codec->encodeWithSig(helloMethod);
        auto fun = toString32(h256(func, FixedBytes<32>::AlignLeft));
        uint8_t type = (_type == AuthType::WHITE_LIST_MODE) ? 1 : 2;
        auto t = toString32(h256(type));
        bytes in = codec->encodeWithSig("setMethodAuthType(address,bytes4,uint8)", _path, fun, t);
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
        params2->setTo(precompiled::CONTRACT_AUTH_ADDRESS);
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
        if (_errorCode != 0)
        {
            BOOST_CHECK(result2->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);
        return result2;
    };

    ExecutionMessage::UniquePtr authMethodAuth(protocol::BlockNumber _number, int _contextId,
        std::string const& authMethod, Address const& _path, std::string const& helloMethod,
        Address const& _account, int _errorCode = 0)
    {
        nextBlock(_number);
        bytes fun = codec->encodeWithSig(helloMethod);
        auto func = toString32(h256(fun, FixedBytes<32>::AlignLeft));
        bytes in = codec->encodeWithSig(authMethod, _path, func, _account);
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
        params2->setTo(precompiled::CONTRACT_AUTH_ADDRESS);
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
        if (_errorCode != 0)
        {
            BOOST_CHECK(result2->data().toBytes() == codec->encode(s256(_errorCode)));
        }
        commitBlock(_number);

        return result2;
    };

    ExecutionMessage::UniquePtr resetAdmin(protocol::BlockNumber _number, int _contextId,
        Address const& _path, Address const& _newAdmin, int _errorCode = 0)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("resetAdmin(address,address)", _path, _newAdmin);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        auto newSender = Address("0000000000000000000000000000000000010001");
        tx->forceSender(newSender.asBytes());
        auto hash = tx->hash();
        txpool->hash2Transaction[hash] = tx;
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(_contextId);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(precompiled::CONTRACT_AUTH_ADDRESS);
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
        if (_errorCode != 0)
        {
            BOOST_CHECK(result2->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);
        return result2;
    };

    std::string sender;
    std::string helloAddress;

    std::string helloBin =
        "608060405234801561001057600080fd5b506040518060400160405280600d81526020017f48656c6c6f2c2057"
        "6f726c6421000000000000000000000000000000000000008152506000908051906020019061005c9291906100"
        "62565b50610107565b828054600181600116156101000203166002900490600052602060002090601f01602090"
        "0481019282601f106100a357805160ff19168380011785556100d1565b828001600101855582156100d1579182"
        "015b828111156100d05782518255916020019190600101906100b5565b5b5090506100de91906100e2565b5090"
        "565b61010491905b808211156101005760008160009055506001016100e8565b5090565b90565b610310806101"
        "166000396000f3fe608060405234801561001057600080fd5b50600436106100365760003560e01c80634ed388"
        "5e1461003b5780636d4ce63c146100f6575b600080fd5b6100f46004803603602081101561005157600080fd5b"
        "810190808035906020019064010000000081111561006e57600080fd5b82018360208201111561008057600080"
        "fd5b803590602001918460018302840111640100000000831117156100a257600080fd5b91908080601f016020"
        "809104026020016040519081016040528093929190818152602001838380828437600081840152601f19601f82"
        "0116905080830192505050505050509192919290505050610179565b005b6100fe610193565b60405180806020"
        "01828103825283818151815260200191508051906020019080838360005b8381101561013e5780820151818401"
        "52602081019050610123565b50505050905090810190601f16801561016b578082038051600183602003610100"
        "0a031916815260200191505b509250505060405180910390f35b806000908051906020019061018f9291906102"
        "35565b5050565b606060008054600181600116156101000203166002900480601f016020809104026020016040"
        "51908101604052809291908181526020018280546001816001161561010002031660029004801561022b578060"
        "1f106102005761010080835404028352916020019161022b565b820191906000526020600020905b8154815290"
        "6001019060200180831161020e57829003601f168201915b5050505050905090565b8280546001816001161561"
        "01000203166002900490600052602060002090601f016020900481019282601f1061027657805160ff19168380"
        "011785556102a4565b828001600101855582156102a4579182015b828111156102a35782518255916020019190"
        "60010190610288565b5b5090506102b191906102b5565b5090565b6102d791905b808211156102d35760008160"
        "009055506001016102bb565b5090565b9056fea2646970667358221220bf4a4547462412a2d27d205b50ba5d4d"
        "ba42f506f9ea3628eb3d0299c9c28d5664736f6c634300060a0033";
};

BOOST_FIXTURE_TEST_SUITE(precompiledPermissionTest, PermissionPrecompiledFixture)

BOOST_AUTO_TEST_CASE(testMethodWhiteList)
{
    deployHello();
    // simple get
    {
        auto result = helloGet(2, 1000);
        BOOST_CHECK(result->data().toBytes() == codec->encode(std::string("Hello, World!")));
    }

    // add method acl type
    {
        BlockNumber _number = 3;
        // set method acl type
        {
            auto result = authSetMethodType(
                _number++, 1000, Address(helloAddress), "get()", AuthType::WHITE_LIST_MODE);
            BOOST_CHECK(result->data().toBytes() == codec->encode(u256(0)));
        }

        // can't get now, even if not set any acl
        {
            auto result = helloGet(_number++, 1000, 0);
            BOOST_CHECK(result->status() == (int32_t)TransactionStatus::PermissionDenied);
            BOOST_CHECK(result->type() == ExecutionMessage::REVERT);
        }

        // can still set
        {
            auto result = helloSet(_number++, 1000, "test1");
            BOOST_CHECK(result->status() == (int32_t)TransactionStatus::None);
        }

        // open white list, only 0x1234567890123456789012345678901234567890 address can use
        {
            auto result4 = authMethodAuth(_number++, 1000, "openMethodAuth(address,bytes4,address)",
                Address(helloAddress), "get()",
                Address("0x1234567890123456789012345678901234567890"));
            BOOST_CHECK(result4->data().toBytes() == codec->encode(u256(0)));
        }

        // get permission denied
        {
            auto result5 = helloGet(_number++, 1000);
            BOOST_CHECK(result5->status() == (int32_t)TransactionStatus::PermissionDenied);
            BOOST_CHECK(result5->type() == ExecutionMessage::REVERT);
        }

        // can still set
        {
            auto result6 = helloSet(_number++, 1000, "test2");
            BOOST_CHECK(result6->status() == (int32_t)TransactionStatus::None);
        }

        // use address 0x1234567890123456789012345678901234567890, success get
        {
            auto result7 =
                helloGet(_number++, 1000, 0, Address("0x1234567890123456789012345678901234567890"));
            BOOST_CHECK(result7->data().toBytes() == codec->encode(std::string("test2")));
        }

        // close white list, 0x1234567890123456789012345678901234567890 address can not use
        {
            auto result4 = authMethodAuth(_number++, 1000,
                "closeMethodAuth(address,bytes4,address)", Address(helloAddress), "get()",
                Address("0x1234567890123456789012345678901234567890"));
            BOOST_CHECK(result4->data().toBytes() == codec->encode(u256(0)));
        }

        // use address 0x1234567890123456789012345678901234567890 get permission denied
        {
            auto result5 =
                helloGet(_number++, 1000, 0, Address("0x1234567890123456789012345678901234567890"));
            BOOST_CHECK(result5->status() == (int32_t)TransactionStatus::PermissionDenied);
            BOOST_CHECK(result5->type() == ExecutionMessage::REVERT);
        }

        // use address 0x1234567890123456789012345678901234567890 still can set
        {
            auto result = helloSet(
                _number++, 1000, "test2", 0, Address("0x1234567890123456789012345678901234567890"));
            BOOST_CHECK(result->status() == (int32_t)TransactionStatus::None);
        }
    }
}

BOOST_AUTO_TEST_CASE(testMethodBlackList)
{
    deployHello();
    // simple get
    {
        auto result = helloGet(3, 1000);
        BOOST_CHECK(result->data().toBytes() == codec->encode(std::string("Hello, World!")));
    }

    // add method acl type
    {
        BlockNumber _number = 4;
        // set method acl type
        {
            auto result = authSetMethodType(
                _number++, 1000, Address(helloAddress), "get()", AuthType::BLACK_LIST_MODE);
            BOOST_CHECK(result->data().toBytes() == codec->encode(u256(0)));
        }

        // still can get now, even if not set any acl
        {
            auto result = helloGet(_number++, 1000, 0);
            BOOST_CHECK(result->data().toBytes() == codec->encode(std::string("Hello, World!")));
        }

        // still can set, even if not set any acl
        {
            auto result = helloSet(_number++, 1000, "test1");
            BOOST_CHECK(result->status() == (int32_t)TransactionStatus::None);
        }

        // still can get now, even if not set any acl
        {
            auto result = helloGet(_number++, 1000, 0);
            BOOST_CHECK(result->data().toBytes() == codec->encode(std::string("test1")));
        }

        // open black list, block 0x1234567890123456789012345678901234567890 address usage
        {
            auto result = authMethodAuth(_number++, 1000, "openMethodAuth(address,bytes4,address)",
                Address(helloAddress), "get()",
                Address("0x1234567890123456789012345678901234567890"));
            BOOST_CHECK(result->data().toBytes() == codec->encode(u256(0)));
        }

        // can still set
        {
            auto result = helloSet(_number++, 1000, "test2");
            BOOST_CHECK(result->status() == (int32_t)TransactionStatus::None);
        }

        // can still get with default address
        {
            auto result = helloGet(_number++, 1000);
            BOOST_CHECK(result->data().toBytes() == codec->encode(std::string("test2")));
        }

        // use address 0x1234567890123456789012345678901234567890, get permission denied
        {
            auto result =
                helloGet(_number++, 1000, 0, Address("0x1234567890123456789012345678901234567890"));
            BOOST_CHECK(result->status() == (int32_t)TransactionStatus::PermissionDenied);
            BOOST_CHECK(result->type() == ExecutionMessage::REVERT);
        }

        // close black list, 0x1234567890123456789012345678901234567890 address can use
        {
            auto result4 = authMethodAuth(_number++, 1000,
                "closeMethodAuth(address,bytes4,address)", Address(helloAddress), "get()",
                Address("0x1234567890123456789012345678901234567890"));
            BOOST_CHECK(result4->data().toBytes() == codec->encode(u256(0)));
        }

        // use address 0x1234567890123456789012345678901234567890, get success
        {
            auto result =
                helloGet(_number++, 1000, 0, Address("0x1234567890123456789012345678901234567890"));
            BOOST_CHECK(result->data().toBytes() == codec->encode(std::string("test2")));
        }
    }
}

BOOST_AUTO_TEST_CASE(testResetAdmin)
{
    deployHello();
    // simple get
    {
        auto result = helloGet(2, 1000);
        BOOST_CHECK(result->data().toBytes() == codec->encode(std::string("Hello, World!")));
    }

    // add method acl type
    {
        BlockNumber _number = 3;
        // set method acl type
        {
            auto result = authSetMethodType(
                _number++, 1000, Address(helloAddress), "get()", AuthType::BLACK_LIST_MODE);
            BOOST_CHECK(result->data().toBytes() == codec->encode(u256(0)));
        }

        // open black list, block 0x1234567890123456789012345678901234567890 address usage
        {
            auto result = authMethodAuth(_number++, 1000, "openMethodAuth(address,bytes4,address)",
                Address(helloAddress), "get()",
                Address("0x1234567890123456789012345678901234567890"));
            BOOST_CHECK(result->data().toBytes() == codec->encode(u256(0)));
        }

        // can still set
        {
            auto result = helloSet(_number++, 1000, "test2");
            BOOST_CHECK(result->status() == (int32_t)TransactionStatus::None);
        }

        // can still get with default address
        {
            auto result = helloGet(_number++, 1000);
            BOOST_CHECK(result->data().toBytes() == codec->encode(std::string("test2")));
        }

        // use address 0x1234567890123456789012345678901234567890, get permission denied
        {
            auto result =
                helloGet(_number++, 1000, 0, Address("0x1234567890123456789012345678901234567890"));
            BOOST_CHECK(result->status() == (int32_t)TransactionStatus::PermissionDenied);
            BOOST_CHECK(result->type() == ExecutionMessage::REVERT);
        }

        // reset admin
        {
            auto result = resetAdmin(_number++, 1000, Address(helloAddress),
                Address("0x1234567890123456789012345678901234567890"));
            BOOST_CHECK(result->data().toBytes() == codec->encode(u256(0)));
        }

        // close black list, 0x1234567890123456789012345678901234567890 address can use
        // permission denied
        {
            auto result = authMethodAuth(_number++, 1000, "closeMethodAuth(address,bytes4,address)",
                Address(helloAddress), "get()",
                Address("0x1234567890123456789012345678901234567890"));
            BOOST_CHECK(result->data().toBytes() == codec->encode(s256((int)CODE_NO_AUTHORIZED)));
        }

        // use address 0x1234567890123456789012345678901234567890, still permission denied
        {
            auto result =
                helloGet(_number++, 1000, 0, Address("0x1234567890123456789012345678901234567890"));
            BOOST_CHECK(result->status() == (int32_t)TransactionStatus::PermissionDenied);
            BOOST_CHECK(result->type() == ExecutionMessage::REVERT);
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test