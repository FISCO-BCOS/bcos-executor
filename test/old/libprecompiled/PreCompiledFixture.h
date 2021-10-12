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
 * @file PreCompiledFixture.h
 * @author: kyonRay
 * @date 2021-06-19
 */

#pragma once
#include "bcos-executor/TransactionExecutor.h"
#include "bcos-framework/interfaces/ledger/LedgerTypeDef.h"
#include "bcos-framework/testutils/protocol/FakeBlock.h"
#include "bcos-framework/testutils/protocol/FakeBlockHeader.h"
#include "executive/BlockContext.h"
#include "executive/TransactionExecutive.h"
#include "mock/MockTransactionalStorage.h"
#include "mock/MockTxPool.h"
#include "precompiled/Utilities.h"
#include "precompiled/extension/UserPrecompiled.h"
#include <bcos-framework/interfaces/storage/Table.h>
#include <bcos-framework/libexecutor/NativeExecutionMessage.h>
#include <bcos-framework/testutils/TestPromptFixture.h>
#include <bcos-framework/testutils/crypto/HashImpl.h>
#include <bcos-framework/testutils/crypto/SignatureImpl.h>
#include <string>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::ledger;

namespace bcos::test
{
class PrecompiledFixture : public TestPromptFixture
{
public:
    PrecompiledFixture()
    {
        hashImpl = std::make_shared<Keccak256Hash>();
        assert(hashImpl);
        smHashImpl = std::make_shared<Sm3Hash>();
        auto signatureImpl = std::make_shared<Secp256k1SignatureImpl>();
        auto sm2Sign = std::make_shared<SM2SignatureImpl>();
        assert(signatureImpl);
        cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
        assert(cryptoSuite);
        smCryptoSuite = std::make_shared<CryptoSuite>(smHashImpl, sm2Sign, nullptr);
        txpool = std::make_shared<MockTxPool>();
        assert(DEFAULT_PERMISSION_ADDRESS);
    }

    virtual ~PrecompiledFixture() {}

    /// must set isWasm
    void setIsWasm(bool _isWasm)
    {
        isWasm = _isWasm;
        storage = std::make_shared<MockTransactionalStorage>(hashImpl);
        memoryTableFactory = std::make_shared<StateStorage>(storage);

        blockFactory = createBlockFactory(cryptoSuite);
        auto header = blockFactory->blockHeaderFactory()->createBlockHeader(1);
        header->setNumber(1);

        auto executionResultFactory = std::make_shared<NativeExecutionMessageFactory>();
        executor = std::make_shared<TransactionExecutor>(
            txpool, storage, executionResultFactory, hashImpl, _isWasm);
        createSysTable();
        context = std::make_shared<BlockContext>(
            memoryTableFactory, hashImpl, header, executionResultFactory, EVMSchedule(), _isWasm);
        codec = std::make_shared<PrecompiledCodec>(hashImpl, context->isWasm());
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
    }

    void setSM(bool _isWasm)
    {
        isWasm = _isWasm;
        storage = std::make_shared<MockTransactionalStorage>(smHashImpl);
        memoryTableFactory = std::make_shared<StateStorage>(storage);

        blockFactory = createBlockFactory(smCryptoSuite);
        auto header = blockFactory->blockHeaderFactory()->createBlockHeader(1);
        header->setNumber(1);

        auto executionResultFactory = std::make_shared<NativeExecutionMessageFactory>();
        executor = std::make_shared<TransactionExecutor>(
            txpool, storage, executionResultFactory, smHashImpl, _isWasm);
        createSysTable();
        context = std::make_shared<BlockContext>(
            memoryTableFactory, smHashImpl, header, executionResultFactory, EVMSchedule(), _isWasm);
        codec = std::make_shared<PrecompiledCodec>(smHashImpl, context->isWasm());

        keyPair = smCryptoSuite->signatureImpl()->generateKeyPair();
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
    }

    void createSysTable()
    {
        // create sys table
        memoryTableFactory->createTable(ledger::SYS_CONFIG, "value,enable_number");
        auto table = memoryTableFactory->openTable(ledger::SYS_CONFIG);
        auto entry = table->newEntry();
        entry.setField(SYS_VALUE, "3000000");
        entry.setField(SYS_CONFIG_ENABLE_BLOCK_NUMBER, "0");
        table->setRow(SYSTEM_KEY_TX_GAS_LIMIT, std::move(entry));


        // create / table
        memoryTableFactory->createTable("/", FS_FIELD_COMBINED);

        // create /tables table
        memoryTableFactory->createTable("/tables", FS_FIELD_COMBINED);
        auto rootTable = memoryTableFactory->openTable("/");
        assert(rootTable != std::nullopt);
        auto dirEntry = rootTable->newEntry();
        dirEntry.setField(FS_FIELD_TYPE, FS_TYPE_DIR);
        dirEntry.setField(FS_FIELD_ACCESS, "");
        dirEntry.setField(FS_FIELD_OWNER, "root");
        dirEntry.setField(FS_FIELD_GID, "/usr");
        dirEntry.setField(FS_FIELD_EXTRA, "");
        rootTable->setRow(getDirBaseName("/tables"), dirEntry);
        rootTable->setRow("/", dirEntry);
    }

    void nextBlock(int64_t blockNumber)
    {
        auto blockHeader = std::make_shared<bcos::protocol::PBBlockHeader>(cryptoSuite);
        blockHeader->setNumber(blockNumber);

        std::promise<void> nextPromise;
        executor->nextBlockHeader(blockHeader, [&](bcos::Error::Ptr&& error) {
            BOOST_CHECK(!error);
            nextPromise.set_value();
        });
        nextPromise.get_future().get();
    }

protected:
    crypto::Hash::Ptr hashImpl;
    crypto::Hash::Ptr smHashImpl;
    BlockContext::Ptr context;
    protocol::BlockFactory::Ptr blockFactory;
    CryptoSuite::Ptr cryptoSuite = nullptr;
    CryptoSuite::Ptr smCryptoSuite = nullptr;

    std::shared_ptr<MockTransactionalStorage> storage;
    StateStorage::Ptr memoryTableFactory;
    TransactionExecutor::Ptr executor;
    std::shared_ptr<MockTxPool> txpool;
    KeyPairInterface::Ptr keyPair;

    PrecompiledCodec::Ptr codec;
    u256 gas = u256(300000000);
    bool isWasm = false;
};
}  // namespace bcos::test
