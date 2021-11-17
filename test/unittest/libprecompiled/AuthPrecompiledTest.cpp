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
        authAddress = Address("0x420f853b49838bd3e9466c85a4cc3428c960dde2").hex();
        helloAddress = Address("0x1234654b49838bd3e9466c85a4cc3428c9601234").hex();
    }

    virtual ~PermissionPrecompiledFixture() {}

    void deployTest()
    {
        bytes input;
        boost::algorithm::unhex(authBin, std::back_inserter(input));
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
        params->setTo(authAddress);
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
        BOOST_CHECK_EQUAL(result->newEVMContractAddress(), authAddress);
        BOOST_CHECK_EQUAL(result->origin(), sender);
        BOOST_CHECK_EQUAL(result->from(), authAddress);
        BOOST_CHECK(result->to() == sender);
        BOOST_CHECK_LT(result->gasAvailable(), gas);

        commitBlock(1);
    }

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
        params2->setTo(authAddress);
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
        result2->setFrom("/sys/" + authAddress);

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
        params2->setTo(authAddress);
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
        result2->setFrom("/sys/" + authAddress);

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

    std::string sender;
    std::string authAddress;
    std::string helloAddress;

    std::string authBin =
        "608060405234801561001057600080fd5b5061099b806100206000396000f3fe60806040523480156100105760"
        "0080fd5b50600436106100625760003560e01c80630c82b73d1461006757806364efb22b146100975780639cc3"
        "ca0f146100c7578063c53057b4146100f7578063cb7c5c1114610127578063d8662aa414610157575b600080fd"
        "5b610081600480360381019061007c919061064e565b610187565b60405161008e9190610856565b6040518091"
        "0390f35b6100b160048036038101906100ac91906105c0565b610228565b6040516100be9190610789565b6040"
        "5180910390f35b6100e160048036038101906100dc919061069d565b6102c1565b6040516100ee919061085656"
        "5b60405180910390f35b610111600480360381019061010c9190610612565b610362565b60405161011e919061"
        "0856565b60405180910390f35b610141600480360381019061013c919061064e565b610400565b60405161014e"
        "9190610856565b60405180910390f35b610171600480360381019061016c919061064e565b6104a1565b604051"
        "61017e919061083b565b60405180910390f35b60008061100590508073ffffffffffffffffffffffffffffffff"
        "ffffffff16630c82b73d8686866040518463ffffffff1660e01b81526004016101cc939291906107cd565b6020"
        "60405180830381600087803b1580156101e657600080fd5b505af11580156101fa573d6000803e3d6000fd5b50"
        "5050506040513d601f19601f8201168201806040525081019061021e9190610715565b9150509392505050565b"
        "60008061100590508073ffffffffffffffffffffffffffffffffffffffff166364efb22b846040518263ffffff"
        "ff1660e01b81526004016102699190610789565b60206040518083038186803b15801561028157600080fd5b50"
        "5afa158015610295573d6000803e3d6000fd5b505050506040513d601f19601f82011682018060405250810190"
        "6102b991906105e9565b915050919050565b60008061100590508073ffffffffffffffffffffffffffffffffff"
        "ffffff16639cc3ca0f8686866040518463ffffffff1660e01b815260040161030693929190610804565b602060"
        "405180830381600087803b15801561032057600080fd5b505af1158015610334573d6000803e3d6000fd5b5050"
        "50506040513d601f19601f820116820180604052508101906103589190610715565b9150509392505050565b60"
        "008061100590508073ffffffffffffffffffffffffffffffffffffffff1663c53057b485856040518363ffffff"
        "ff1660e01b81526004016103a59291906107a4565b602060405180830381600087803b1580156103bf57600080"
        "fd5b505af11580156103d3573d6000803e3d6000fd5b505050506040513d601f19601f82011682018060405250"
        "8101906103f79190610715565b91505092915050565b60008061100590508073ffffffffffffffffffffffffff"
        "ffffffffffffff1663cb7c5c118686866040518463ffffffff1660e01b8152600401610445939291906107cd56"
        "5b602060405180830381600087803b15801561045f57600080fd5b505af1158015610473573d6000803e3d6000"
        "fd5b505050506040513d601f19601f820116820180604052508101906104979190610715565b91505093925050"
        "50565b60008061100590508073ffffffffffffffffffffffffffffffffffffffff1663d8662aa4868686604051"
        "8463ffffffff1660e01b81526004016104e6939291906107cd565b602060405180830381600087803b15801561"
        "050057600080fd5b505af1158015610514573d6000803e3d6000fd5b505050506040513d601f19601f82011682"
        "01806040525081019061053891906106ec565b9150509392505050565b600081359050610551816108f2565b92"
        "915050565b600081519050610566816108f2565b92915050565b60008151905061057b81610909565b92915050"
        "565b60008135905061059081610920565b92915050565b6000815190506105a581610937565b92915050565b60"
        "00813590506105ba8161094e565b92915050565b6000602082840312156105d257600080fd5b60006105e08482"
        "8501610542565b91505092915050565b6000602082840312156105fb57600080fd5b6000610609848285016105"
        "57565b91505092915050565b6000806040838503121561062557600080fd5b600061063385828601610542565b"
        "925050602061064485828601610542565b9150509250929050565b600080600060608486031215610663576000"
        "80fd5b600061067186828701610542565b935050602061068286828701610581565b9250506040610693868287"
        "01610542565b9150509250925092565b6000806000606084860312156106b257600080fd5b60006106c0868287"
        "01610542565b93505060206106d186828701610581565b92505060406106e2868287016105ab565b9150509250"
        "925092565b6000602082840312156106fe57600080fd5b600061070c8482850161056c565b9150509291505056"
        "5b60006020828403121561072757600080fd5b600061073584828501610596565b91505092915050565b610747"
        "81610871565b82525050565b61075681610883565b82525050565b6107658161088f565b82525050565b610774"
        "816108bb565b82525050565b610783816108e5565b82525050565b600060208201905061079e60008301846107"
        "3e565b92915050565b60006040820190506107b9600083018561073e565b6107c6602083018461073e565b9392"
        "505050565b60006060820190506107e2600083018661073e565b6107ef602083018561075c565b6107fc604083"
        "018461073e565b949350505050565b6000606082019050610819600083018661073e565b610826602083018561"
        "075c565b610833604083018461077a565b949350505050565b6000602082019050610850600083018461074d56"
        "5b92915050565b600060208201905061086b600083018461076b565b92915050565b600061087c826108c5565b"
        "9050919050565b60008115159050919050565b60007fffffffff00000000000000000000000000000000000000"
        "00000000000000000082169050919050565b6000819050919050565b600073ffffffffffffffffffffffffffff"
        "ffffffffffff82169050919050565b600060ff82169050919050565b6108fb81610871565b8114610906576000"
        "80fd5b50565b61091281610883565b811461091d57600080fd5b50565b6109298161088f565b81146109345760"
        "0080fd5b50565b610940816108bb565b811461094b57600080fd5b50565b610957816108e5565b811461096257"
        "600080fd5b5056fea2646970667358221220841fb6a500d56e6356817a95b2470fbbfd3513c05a4fe4d7042e10"
        "0aef39722b64736f6c634300060a0033";

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
    deployTest();
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
    deployTest();
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

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test