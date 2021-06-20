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
 * @author: xingqiangbai
 * @date: 2021-06-09
 */

#include "libexecutor/Executor.h"
#include "../MemoryStorage.h"
#include "../mock/MockDispatcher.h"
#include "../mock/MockLedger.h"
#include "bcos-framework/interfaces/ledger/LedgerTypeDef.h"
#include "bcos-framework/testutils/crypto/HashImpl.h"
#include "bcos-framework/testutils/crypto/SignatureImpl.h"
#include "bcos-framework/testutils/protocol/FakeBlock.h"
#include "bcos-framework/testutils/protocol/FakeBlockHeader.h"
#include "libprecompiled/Common.h"
#include "libvm/Executive.h"
#include "libvm/BlockContext.h"
#include <boost/test/unit_test.hpp>
#include <iostream>
#include <set>

using namespace std;
using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::precompiled;

namespace bcos
{
namespace test
{
struct ExecutorFixture
{
    ExecutorFixture()
    {
        auto hashImpl = std::make_shared<Keccak256Hash>();
        assert(hashImpl);
        auto signatureImpl = std::make_shared<Secp256k1SignatureImpl>();
        assert(signatureImpl);
        cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
        assert(cryptoSuite);
        blockFactory = createBlockFactory(cryptoSuite);
        auto header = blockFactory->blockHeaderFactory()->createBlockHeader(1);
        header->setNumber(1);
        ledger = make_shared<MockLedger>(header, blockFactory);
        storage = make_shared<MemoryStorage>();
        dispatcher = make_shared<MockDispatcher>();
        executor = make_shared<Executor>(blockFactory, dispatcher, ledger, storage, false);
        // create sys table
        auto tableFactory = std::make_shared<TableFactory>(storage, hashImpl, 0);
        tableFactory->createTable(ledger::SYS_CONFIG, SYS_KEY, "value,enable_number");
        auto table = tableFactory->openTable(ledger::SYS_CONFIG);
        auto entry = table->newEntry();
        entry->setField(SYS_VALUE, "3000000");
        entry->setField(SYS_CONFIG_ENABLE_BLOCK_NUMBER, "0");
        table->setRow(SYSTEM_KEY_TX_GAS_LIMIT, entry);
        tableFactory->commit();
        executiveContext = executor->createExecutiveContext(header);
    }
    CryptoSuite::Ptr cryptoSuite = nullptr;
    protocol::BlockFactory::Ptr blockFactory;
    MockLedger::Ptr ledger;
    MemoryStorage::Ptr storage;
    MockDispatcher::Ptr dispatcher;
    Executor::Ptr executor;
    BlockContext::Ptr executiveContext = nullptr;
    string helloBin =
        "0x60806040526040805190810160405280600181526020017f3100000000000000000000000000000000000000"
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
BOOST_FIXTURE_TEST_SUITE(ExecutorTest, ExecutorFixture)

BOOST_AUTO_TEST_CASE(construct)
{
    executor = make_shared<Executor>(blockFactory, dispatcher, ledger, storage, true);
}

BOOST_AUTO_TEST_CASE(executeTransaction_DeployHelloWorld)
{
    auto keyPair = cryptoSuite->signatureImpl()->generateKeyPair();
    memcpy(keyPair->secretKey()->mutableData(),
        fromHexString("ff6f30856ad3bae00b1169808488502786a13e3c174d85682135ffd51310310e")->data(),
        32);
    memcpy(keyPair->publicKey()->mutableData(),
        fromHexString("ccd8de502ac45462767e649b462b5f4ca7eadd69c7e1f1b410bdf754359be29b1b88ffd79744"
                      "03f56e250af52b25682014554f7b3297d6152401e85d426a06ae")
            ->data(),
        64);
    cout << keyPair->secretKey()->hex() << endl << keyPair->publicKey()->hex() << endl;
    auto to = keyPair->address(cryptoSuite->hashImpl()).asBytes();
    auto helloworld = string(helloBin);

    auto input = *fromHexString(helloworld);
    auto tx = fakeTransaction(cryptoSuite, keyPair, bytes(), input, 101, 100001, "1", "1");
    auto executive = std::make_shared<Executive>(executiveContext);
    auto receipt = executor->executeTransaction(tx, executive);
    BOOST_TEST(receipt->status() == (int32_t)TransactionStatus::None);
    BOOST_TEST(receipt->gasUsed() == 430575);
    BOOST_TEST(receipt->hash().hexPrefixed() ==
               "0xaebfa6e88818037c65afed2d33c9cd634cdf0d7f4d0d4e5084af72185706aa28");
    BOOST_TEST(
        *toHexString(receipt->contractAddress()) == "8968b494f66b2508330b24a7d1cafa06a14f6315");
    BOOST_TEST(*toHexString(receipt->output()) == "");
    BOOST_TEST(receipt->blockNumber() == 1);
    auto newAddress = receipt->contractAddress().toBytes();
    // call helloworld get
    input = *fromHexString("0x6d4ce63c");
    auto getTx = fakeTransaction(cryptoSuite, keyPair, newAddress, input, 101, 100001, "1", "1");
    receipt = executor->executeTransaction(getTx, executive);
    BOOST_TEST(receipt->status() == (int32_t)TransactionStatus::None);
    BOOST_TEST(receipt->gasUsed() == 22742);
    BOOST_TEST(receipt->hash().hexPrefixed() ==
               "0xe86887f45811fc1c0862cce8aa66429bcb8845fa56ace5cad406a2d2fb89cb57");
    BOOST_TEST(*toHexString(receipt->contractAddress()) == "");
    // Hello, World! == 48656c6c6f2c20576f726c6421
    BOOST_TEST(*toHexString(receipt->output()) ==
               "00000000000000000000000000000000000000000000000000000000000000200000000000000000000"
               "00000000000000000000000000000000000000000000d48656c6c6f2c20576f726c6421000000000000"
               "00000000000000000000000000");
    BOOST_TEST(receipt->blockNumber() == 1);

    // call helloworld set fisco
    input = *fromHexString(
        "0x4ed3885e00000000000000000000000000000000000000000000000000000000000000200000000000000000"
        "000000000000000000000000000000000000000000000005666973636f00000000000000000000000000000000"
        "0000000000000000000000");
    auto setTx = fakeTransaction(cryptoSuite, keyPair, newAddress, input, 101, 100001, "1", "1");
    receipt = executor->executeTransaction(setTx, executive);
    BOOST_TEST(receipt->status() == (int32_t)TransactionStatus::None);
    BOOST_TEST(receipt->gasUsed() == 30791);
    BOOST_TEST(receipt->hash().hexPrefixed() ==
               "0x407edb8616a3772a18c89fb7da1326c9b0d690ba1b60cf3942d86383b6a62b0a");
    BOOST_TEST(*toHexString(receipt->contractAddress()) == "");
    BOOST_TEST(*toHexString(receipt->output()) == "");
    BOOST_TEST(receipt->blockNumber() == 1);
    // get
    receipt = executor->executeTransaction(getTx, executive);
    // Hello, World! == 666973636f
    BOOST_TEST(*toHexString(receipt->output()) ==
               "00000000000000000000000000000000000000000000000000000000000000200000000000000000000"
               "000000000000000000000000000000000000000000005666973636f0000000000000000000000000000"
               "00000000000000000000000000");
}

BOOST_AUTO_TEST_CASE(executeBlock)
{
    auto block = blockFactory->createBlock();
    auto header = blockFactory->blockHeaderFactory()->createBlockHeader(1);
    block->setBlockHeader(header);
    auto keyPair = cryptoSuite->signatureImpl()->generateKeyPair();
    memcpy(keyPair->secretKey()->mutableData(),
        fromHexString("ff6f30856ad3bae00b1169808488502786a13e3c174d85682135ffd51310310e")->data(),
        32);
    memcpy(keyPair->publicKey()->mutableData(),
        fromHexString("ccd8de502ac45462767e649b462b5f4ca7eadd69c7e1f1b410bdf754359be29b1b88ffd79744"
                      "03f56e250af52b25682014554f7b3297d6152401e85d426a06ae")
            ->data(),
        64);
    auto to = keyPair->address(cryptoSuite->hashImpl()).asBytes();
    auto helloworld = string(helloBin);
    auto input = *fromHexString(helloworld);
    auto tx = fakeTransaction(cryptoSuite, keyPair, bytes(), input, 101, 100001, "1", "1");
    block->appendTransaction(tx);
    tx = fakeTransaction(cryptoSuite, keyPair, bytes(), input, 102, 100002, "1", "1");
    block->appendTransaction(tx);
    auto getTx = fakeTransaction(cryptoSuite, keyPair,
        *fromHexString("8968b494f66b2508330b24a7d1cafa06a14f6315"), *fromHexString("0x6d4ce63c"),
        101, 100001, "1", "1");
    block->appendTransaction(getTx);
    block->setBlockType(BlockType::CompleteBlock);
    auto txsRoot = block->blockHeader()->txsRoot();
    auto receiptsRoot = block->blockHeader()->receiptsRoot();
    auto stateRoot = block->blockHeader()->stateRoot();
    BOOST_TEST(block->blockHeader()->gasUsed() == 0);

    // execute block
    auto parentHeader = blockFactory->blockHeaderFactory()->createBlockHeader(0);
    auto result = executor->executeBlock(block, parentHeader);
    auto deployReceipt = block->receipt(0);
    BOOST_TEST(deployReceipt->status() == (int32_t)TransactionStatus::None);
    BOOST_TEST(deployReceipt->gasUsed() == 430575);
    BOOST_TEST(deployReceipt->hash().hexPrefixed() ==
               "0xaebfa6e88818037c65afed2d33c9cd634cdf0d7f4d0d4e5084af72185706aa28");
    BOOST_TEST(*toHexString(deployReceipt->contractAddress()) ==
               "8968b494f66b2508330b24a7d1cafa06a14f6315");
    BOOST_TEST(*toHexString(deployReceipt->output()) == "");
    BOOST_TEST(deployReceipt->blockNumber() == 1);

    deployReceipt = block->receipt(1);
    BOOST_TEST(deployReceipt->status() == (int32_t)TransactionStatus::None);
    BOOST_TEST(deployReceipt->gasUsed() == 430575);
    BOOST_TEST(deployReceipt->hash().hexPrefixed() ==
               "0xf3e7e4895752ef0d6c1ba617801790732f7fc6424f08d61cae52d315beb4d408");
    BOOST_TEST(*toHexString(deployReceipt->contractAddress()) ==
               "21f7f2c888221d771e103cb2e56a7da15a2d898e");
    BOOST_TEST(*toHexString(deployReceipt->output()) == "");
    BOOST_TEST(deployReceipt->blockNumber() == 1);

    auto getReceipt = block->receipt(2);
    BOOST_TEST(getReceipt->status() == (int32_t)TransactionStatus::None);
    BOOST_TEST(getReceipt->gasUsed() == 22742);
    BOOST_TEST(getReceipt->hash().hexPrefixed() ==
               "0xe86887f45811fc1c0862cce8aa66429bcb8845fa56ace5cad406a2d2fb89cb57");
    BOOST_TEST(*toHexString(getReceipt->contractAddress()) == "");
    // Hello, World! == 48656c6c6f2c20576f726c6421
    BOOST_TEST(*toHexString(getReceipt->output()) ==
               "00000000000000000000000000000000000000000000000000000000000000200000000000000000000"
               "00000000000000000000000000000000000000000000d48656c6c6f2c20576f726c6421000000000000"
               "00000000000000000000000000");
    BOOST_TEST(getReceipt->blockNumber() == 1);

    // TODO: check block
    BOOST_TEST(block->blockType() == BlockType::CompleteBlock);
    BOOST_TEST(block->blockHeader()->gasUsed() == 883892);
    BOOST_TEST(txsRoot == block->blockHeader()->txsRoot());
    BOOST_TEST(receiptsRoot != block->blockHeader()->receiptsRoot());
    BOOST_TEST(block->blockHeader()->stateRoot() != stateRoot);

    BOOST_TEST(block->blockHeader()->receiptsRoot().hexPrefixed() ==
               "0x5838cfc06b6881518763d5af6347dd654de2df2d1862062ea87046884bc8069e");
    BOOST_TEST(block->blockHeader()->stateRoot().hexPrefixed() ==
               "0x9c14ee6b56c187aa4d064583b1bdc0e954cc59f5e2ad5840a9f702d7851b7fd1");
    BOOST_TEST(block->blockHeader()->stateRoot().hexPrefixed() ==
               result->getTableFactory()->hash().hexPrefixed());
}


BOOST_AUTO_TEST_CASE(start_stop)
{
    executor = make_shared<Executor>(blockFactory, dispatcher, ledger, storage, true);
    executor->start();

    executor->stop();
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
