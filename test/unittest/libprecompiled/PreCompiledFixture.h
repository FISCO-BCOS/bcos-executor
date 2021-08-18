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
#include "../MemoryStorage.h"
#include "bcos-executor/Executor.h"
#include "bcos-framework/interfaces/ledger/LedgerTypeDef.h"
#include "bcos-framework/testutils/protocol/FakeBlock.h"
#include "bcos-framework/testutils/protocol/FakeBlockHeader.h"
#include "libprecompiled/Utilities.h"
#include "libprecompiled/extension/UserPrecompiled.h"
#include "libvm/BlockContext.h"
#include "libvm/TransactionExecutive.h"
#include "mock/MockDispatcher.h"
#include "mock/MockLedger.h"
#include <bcos-framework/interfaces/storage/TableInterface.h>
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
        assert(signatureImpl);
        cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
        assert(cryptoSuite);
        dispatcher = std::make_shared<MockDispatcher>();
        storage = std::make_shared<MemoryStorage>();

        // create sys table
        memoryTableFactory = std::make_shared<TableFactory>(storage, hashImpl, 0);
        memoryTableFactory->createTable(ledger::SYS_CONFIG, SYS_KEY, "value,enable_number");
        auto table = memoryTableFactory->openTable(ledger::SYS_CONFIG);
        auto entry = table->newEntry();
        entry->setField(SYS_VALUE, "3000000");
        entry->setField(SYS_CONFIG_ENABLE_BLOCK_NUMBER, "0");
        table->setRow(SYSTEM_KEY_TX_GAS_LIMIT, entry);

        // create /data table
        memoryTableFactory->createTable("/data", SYS_KEY, SYS_VALUE);
        auto dataTable = memoryTableFactory->openTable("/data");
        auto dirEntry = table->newEntry();
        dirEntry->setField(SYS_VALUE, FS_TYPE_DIR);
        dataTable->setRow(FS_KEY_TYPE, dirEntry);
        auto subEntry = table->newEntry();
        subEntry->setField(SYS_VALUE, DirInfo::emptyDirString());
        dataTable->setRow(FS_KEY_SUB, subEntry);
        auto numEntry = table->newEntry();
        numEntry->setField(SYS_VALUE, "0");
        dataTable->setRow(FS_KEY_NUM, numEntry);

        // create / table
        memoryTableFactory->createTable("/", SYS_KEY, SYS_VALUE);
        auto rootTable = memoryTableFactory->openTable("/");
        auto rootTypeEntry = rootTable->newEntry();
        rootTypeEntry->setField(SYS_VALUE, FS_TYPE_DIR);
        rootTable->setRow(FS_KEY_TYPE, rootTypeEntry);
        FileInfo dataInfo("data", FS_TYPE_DIR, 0);
        DirInfo rootSubDir({dataInfo});
        auto rootSubEntry = rootTable->newEntry();
        rootSubEntry->setField(SYS_VALUE, rootSubDir.toString());
        rootTable->setRow(FS_KEY_SUB, rootSubEntry);
        auto rootNumEntry = table->newEntry();
        rootNumEntry->setField(SYS_VALUE, "0");
        rootTable->setRow(FS_KEY_NUM, rootNumEntry);

        memoryTableFactory->commit();
    }

    virtual ~PrecompiledFixture() {}

    /// must set isWasm
    void setIsWasm(bool _isWasm)
    {
        isWasm = _isWasm;

        blockFactory = createBlockFactory(cryptoSuite);
        auto header = blockFactory->blockHeaderFactory()->createBlockHeader(1);
        header->setNumber(1);
        ledger = std::make_shared<MockLedger>(header, blockFactory);

        executor = std::make_shared<Executor>(blockFactory, dispatcher, ledger, storage, isWasm);
        auto tableFactory = std::make_shared<TableFactory>(storage, hashImpl, 1);
        context = executor->createExecutiveContext(header, tableFactory);
        codec = std::make_shared<PrecompiledCodec>(hashImpl, context->isWasm());
    }

protected:
    crypto::Hash::Ptr hashImpl;
    crypto::Hash::Ptr smHashImpl;
    BlockContext::Ptr context;
    protocol::BlockFactory::Ptr blockFactory;
    CryptoSuite::Ptr cryptoSuite = nullptr;
    MemoryStorage::Ptr storage;
    MockLedger::Ptr ledger;
    TableFactoryInterface::Ptr memoryTableFactory;
    MockDispatcher::Ptr dispatcher;
    Executor::Ptr executor;
    PrecompiledCodec::Ptr codec;
    u256 gas = u256(300000000);
    bool isWasm = false;
};
}  // namespace bcos::test
