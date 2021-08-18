/*
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
 * @brief host context
 * @file HostContext.cpp
 * @author: xingqiangbai
 * @date: 2021-05-24
 */

#include "HostContext.h"
#include "../libstate/StateInterface.h"
#include "BlockContext.h"
#include "EVMHostInterface.h"
#include "bcos-framework/interfaces/storage/TableInterface.h"
#include "evmc/evmc.hpp"
#include <boost/algorithm/string/split.hpp>
#include <boost/thread.hpp>
#include <algorithm>
#include <exception>
#include <limits>
#include <sstream>
#include <vector>

using namespace std;
using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::protocol;

namespace  // anonymous
{
static unsigned const c_depthLimit = 1024;

/// Upper bound of stack space needed by single CALL/CREATE execution. Set experimentally.
static size_t const c_singleExecutionStackSize = 100 * 1024;

static const std::string SYS_ASSET_NAME = "name";
static const std::string SYS_ASSET_FUNGIBLE = "fungible";
static const std::string SYS_ASSET_TOTAL = "total";
static const std::string SYS_ASSET_SUPPLIED = "supplied";
static const std::string SYS_ASSET_ISSUER = "issuer";
static const std::string SYS_ASSET_DESCRIPTION = "description";
static const std::string SYS_ASSET_INFO = "_sys_asset_info_";

/// Standard thread stack size.
static size_t const c_defaultStackSize =
#if defined(__linux)
    8 * 1024 * 1024;
#elif defined(_WIN32)
    16 * 1024 * 1024;
#else
    512 * 1024;  // OSX and other OSs
#endif

/// Stack overhead prior to allocation.
static size_t const c_entryOverhead = 128 * 1024;

/// On what depth execution should be offloaded to additional separated stack space.
static unsigned const c_offloadPoint =
    (c_defaultStackSize - c_entryOverhead) / c_singleExecutionStackSize;

void goOnOffloadedStack(TransactionExecutive& _e)
{
    // Set new stack size enouth to handle the rest of the calls up to the limit.
    boost::thread::attributes attrs;
    attrs.set_stack_size((c_depthLimit - c_offloadPoint) * c_singleExecutionStackSize);

    // Create new thread with big stack and join immediately.
    // TODO: It is possible to switch the implementation to Boost.Context or similar when the API is
    // stable.
    boost::exception_ptr exception;
    boost::thread{attrs,
        [&] {
            try
            {
                _e.go();
            }
            catch (...)
            {
                exception = boost::current_exception();  // Catch all exceptions to be rethrown in
                                                         // parent thread.
            }
        }}
        .join();
    if (exception)
        boost::rethrow_exception(exception);
}

void go(unsigned _depth, TransactionExecutive& _e)
{
    // If in the offloading point we need to switch to additional separated stack space.
    // Current stack is too small to handle more CALL/CREATE executions.
    // It needs to be done only once as newly allocated stack space it enough to handle
    // the rest of the calls up to the depth limit (c_depthLimit).

    if (_depth == c_offloadPoint)
    {
        EXECUTIVE_LOG(TRACE) << "Stack offloading (depth: " << c_offloadPoint << ")";
        goOnOffloadedStack(_e);
    }
    else
        _e.go();
}

void generateCallResult(
    evmc_result* o_result, evmc_status_code status, u256 gas, owning_bytes_ref&& output)
{
    o_result->status_code = status;
    o_result->gas_left = static_cast<int64_t>(gas);

    // Pass the output to the EVM without a copy. The EVM will delete it
    // when finished with it.

    // First assign reference. References are not invalidated when vector
    // of bytes is moved. See `.takeBytes()` below.
    o_result->output_data = output.data();
    o_result->output_size = output.size();

    // Place a new vector of bytes containing output in result's reserved memory.
    auto* data = evmc_get_optional_storage(o_result);
    static_assert(sizeof(bytes) <= sizeof(*data), "Vector is too big");
    new (data) bytes(output.takeBytes());
    // Set the destructor to delete the vector.
    o_result->release = [](evmc_result const* _result) {
        // check _result is not null
        if (_result == NULL)
        {
            return;
        }
        auto* data = evmc_get_const_optional_storage(_result);
        auto& output = reinterpret_cast<bytes const&>(*data);
        // Explicitly call vector's destructor to release its data.
        // This is normal pattern when placement new operator is used.
        output.~bytes();
    };
}

void generateCreateResult(evmc_result* o_result, evmc_status_code status, u256 gas,
    owning_bytes_ref&& output, const std::string& address)
{
    if (status == EVMC_SUCCESS)
    {
        o_result->status_code = status;
        o_result->gas_left = static_cast<int64_t>(gas);
        o_result->release = nullptr;
        o_result->create_address = toEvmC(address);
        o_result->output_data = nullptr;
        o_result->output_size = 0;
    }
    else
        generateCallResult(o_result, status, gas, std::move(output));
}

evmc_status_code transactionStatusToEvmcStatus(TransactionStatus ex) noexcept
{
    switch (ex)
    {
    case TransactionStatus::None:
        return EVMC_SUCCESS;

    case TransactionStatus::RevertInstruction:
        return EVMC_REVERT;

    case TransactionStatus::OutOfGas:
        return EVMC_OUT_OF_GAS;

    case TransactionStatus::BadInstruction:
        return EVMC_UNDEFINED_INSTRUCTION;

    case TransactionStatus::OutOfStack:
        return EVMC_STACK_OVERFLOW;

    case TransactionStatus::StackUnderflow:
        return EVMC_STACK_UNDERFLOW;

    case TransactionStatus::BadJumpDestination:
        return EVMC_BAD_JUMP_DESTINATION;

    default:
        return EVMC_FAILURE;
    }
}

}  // anonymous namespace

namespace bcos
{
namespace executor
{
namespace
{
evmc_gas_metrics ethMetrics{32000, 20000, 5000, 200, 9000, 2300, 25000};
crypto::Hash::Ptr g_hashImpl = nullptr;
evmc_bytes32 evm_hash_fn(const uint8_t* data, size_t size)
{
    return toEvmC(g_hashImpl->hash(bytesConstRef(data, size)));
}
}  // namespace


HostContext::HostContext(const std::shared_ptr<BlockContext>& _envInfo,
    const std::string_view& _myAddress, const std::string_view& _caller,
    const std::string_view& _origin, bytesConstRef _data, const std::shared_ptr<bytes>& _code,
    h256 const& _codeHash, unsigned _depth, bool _isCreate, bool _staticCall)
  : m_blockContext(_envInfo),
    m_myAddress(_myAddress),
    m_caller(_caller),
    m_origin(_origin),
    m_data(_data),
    m_code(_code),
    m_codeHash(_codeHash),
    m_depth(_depth),
    m_isCreate(_isCreate),
    m_staticCall(_staticCall),
    m_s(_envInfo->getState())
{
    m_tableFactory = m_blockContext->getTableFactory();
    interface = getHostInterface();
    g_hashImpl = m_blockContext->hashHandler();
    // FIXME: rename sm3_hash_fn to evm_hash_fn and add a context pointer to get hashImpl
    sm3_hash_fn = evm_hash_fn;
    version = 0x03000000;

    metrics = &ethMetrics;
}

evmc_result HostContext::call(CallParameters& _p)
{
    TransactionExecutive e{getBlockContext(), depth() + 1};
    stringstream ss;
    // Note: When create TransactionExecutive, the flags of evmc context must be passed
    if (!e.call(_p, origin()))
    {
        go(depth(), e);
        e.accrueSubState(sub());
    }
    _p.gas = e.gas();

    evmc_result evmcResult;
    generateCallResult(
        &evmcResult, transactionStatusToEvmcStatus(e.status()), _p.gas, e.takeOutput());
    return evmcResult;
}

size_t HostContext::codeSizeAt(const std::string_view& _a)
{
    string precompiledAddress;
    if (m_blockContext->isWasm())
    {
        precompiledAddress = string(_a);
    }
    else
    {
        precompiledAddress = toHexStringWithPrefix(_a);
    }
    if (m_blockContext->isPrecompiled(precompiledAddress))
    {
        return 1;
    }
    return m_s->codeSize(_a);
}

h256 HostContext::codeHashAt(const std::string_view& _a)
{
    return exists(_a) ? m_s->codeHash(_a) : h256{};
}

bool HostContext::isPermitted()
{
    // check authority by tx.origin
    if (!m_s->checkAuthority(origin(), myAddress()))
    {
        EXECUTIVE_LOG(ERROR) << "isPermitted PermissionDenied" << LOG_KV("origin", origin())
                             << LOG_KV("address", myAddress());
        return false;
    }
    return true;
}

void HostContext::setStore(u256 const& _n, u256 const& _v)
{
    m_s->setStorage(myAddress(), _n.str(), _v.str());
}

evmc_result HostContext::create(u256& io_gas, bytesConstRef _code, evmc_opcode _op, u256 _salt)
{  // TODO: if liquid support contract create contract add a branch
    TransactionExecutive e{getBlockContext(), depth() + 1};
    // Note: When create TransactionExecutive, the flags of evmc context must be passed
    bool result = false;
    if (_op == evmc_opcode::OP_CREATE)
        result = e.createOpcode(myAddress(), io_gas, _code, origin());
    else
    {
        // TODO: when new CREATE opcode added, this logic maybe affected
        assert(_op == evmc_opcode::OP_CREATE2);
        result = e.create2Opcode(myAddress(), io_gas, _code, origin(), _salt);
    }

    if (!result)
    {
        go(depth(), e);
        e.accrueSubState(sub());
    }
    io_gas = e.gas();
    evmc_result evmcResult;
    generateCreateResult(&evmcResult, transactionStatusToEvmcStatus(e.status()), io_gas,
        e.takeOutput(), e.newAddress());
    return evmcResult;
}

void HostContext::log(h256s&& _topics, bytesConstRef _data)
{
    if (m_blockContext->isWasm() || m_myAddress.empty())
    {
        m_sub.logs->push_back(
            protocol::LogEntry(bytes(m_myAddress.data(), m_myAddress.data() + m_myAddress.size()),
                std::move(_topics), _data.toBytes()));
    }
    else
    {
        // convert solidity address to hex string
        auto hexAddress = *toHexString(m_myAddress);
        boost::algorithm::to_lower(hexAddress);  // this is in case of toHexString be modified
        toChecksumAddress(hexAddress, m_blockContext->hashHandler()->hash(hexAddress).hex());
        m_sub.logs->push_back(
            protocol::LogEntry(asBytes(hexAddress), std::move(_topics), _data.toBytes()));
    }
}

void HostContext::suicide(const std::string_view& _a)
{
    // Why transfer is not used here? That caused a consensus issue before (see Quirk #2 in
    // http://martin.swende.se/blog/Ethereum_quirks_and_vulns.html). There is one test case
    // witnessing the current consensus
    // 'GeneralStateTests/stSystemOperationsTest/suicideSendEtherPostDeath.json'.

    // No balance here in BCOS. Balance has data racing in parallel suicide.
    m_sub.suicides.insert(m_myAddress);
    (void)_a;
    return;
    // FIXME: check the logic below
    // m_s->addBalance(_a, m_s->balance(myAddress()));
    // m_s->setBalance(myAddress(), 0);
    // m_sub.suicides.insert(m_myAddress);
}

h256 HostContext::blockHash(int64_t _number)
{
    return getBlockContext()->numberHash(_number);
}

bool HostContext::registerAsset(const std::string& _assetName, const std::string_view& _addr,
    bool _fungible, uint64_t _total, const std::string& _description)
{
    auto table = m_tableFactory->openTable(SYS_ASSET_INFO);
    auto entry = table->getRow(_assetName);
    if (!entry)
    {
        return false;
    }
    entry = table->newEntry();
    entry->setField(SYS_ASSET_NAME, _assetName);
    entry->setField(SYS_ASSET_ISSUER, string(_addr));
    entry->setField(SYS_ASSET_FUNGIBLE, to_string(_fungible));
    entry->setField(SYS_ASSET_TOTAL, to_string(_total));
    entry->setField(SYS_ASSET_SUPPLIED, "0");
    entry->setField(SYS_ASSET_DESCRIPTION, _description);
    auto count = table->setRow(_assetName, entry);
    return count == 1;
}

bool HostContext::issueFungibleAsset(
    const std::string_view& _to, const std::string& _assetName, uint64_t _amount)
{
    auto table = m_tableFactory->openTable(SYS_ASSET_INFO);
    auto entry = table->getRow(_assetName);
    if (!entry)
    {
        EXECUTIVE_LOG(WARNING) << "issueFungibleAsset " << _assetName << "is not exist";
        return false;
    }

    auto issuer = std::string(entry->getField(SYS_ASSET_ISSUER));
    if (caller() != issuer)
    {
        EXECUTIVE_LOG(WARNING) << "issueFungibleAsset not issuer of " << _assetName
                               << LOG_KV("issuer", issuer) << LOG_KV("caller", caller());
        return false;
    }
    // TODO: check supplied is less than total_supply
    auto total = boost::lexical_cast<uint64_t>(entry->getField(SYS_ASSET_TOTAL));
    auto supplied = boost::lexical_cast<uint64_t>(entry->getField(SYS_ASSET_SUPPLIED));
    if (total - supplied < _amount)
    {
        EXECUTIVE_LOG(WARNING) << "issueFungibleAsset overflow total supply"
                               << LOG_KV("amount", _amount) << LOG_KV("supplied", supplied)
                               << LOG_KV("total", total);
        return false;
    }
    // TODO: update supplied
    auto updateEntry = table->newEntry();
    updateEntry->setField(SYS_ASSET_SUPPLIED, to_string(supplied + _amount));
    table->setRow(_assetName, updateEntry);
    // TODO: create new tokens
    depositFungibleAsset(_to, _assetName, _amount);
    return true;
}

uint64_t HostContext::issueNotFungibleAsset(
    const std::string_view& _to, const std::string& _assetName, const std::string& _uri)
{
    // check issuer
    auto table = m_tableFactory->openTable(SYS_ASSET_INFO);
    auto entry = table->getRow(_assetName);
    if (!entry)
    {
        EXECUTIVE_LOG(WARNING) << "issueNotFungibleAsset " << _assetName << "is not exist";
        return false;
    }

    auto issuer = std::string(entry->getField(SYS_ASSET_ISSUER));
    if (caller() != issuer)
    {
        EXECUTIVE_LOG(WARNING) << "issueNotFungibleAsset not issuer of " << _assetName;
        return false;
    }
    // check supplied
    auto total = boost::lexical_cast<uint64_t>(entry->getField(SYS_ASSET_TOTAL));
    auto supplied = boost::lexical_cast<uint64_t>(entry->getField(SYS_ASSET_SUPPLIED));
    if (total - supplied == 0)
    {
        EXECUTIVE_LOG(WARNING) << "issueNotFungibleAsset overflow total supply"
                               << LOG_KV("supplied", supplied) << LOG_KV("total", total);
        return false;
    }
    // get asset id and update supplied
    auto assetID = supplied + 1;
    auto updateEntry = table->newEntry();
    updateEntry->setField(SYS_ASSET_SUPPLIED, to_string(assetID));
    table->setRow(_assetName, updateEntry);

    // create new tokens
    depositNotFungibleAsset(_to, _assetName, assetID, _uri);
    return assetID;
}

void HostContext::depositFungibleAsset(
    const std::string_view& _to, const std::string& _assetName, uint64_t _amount)
{
    auto tableName = getContractTableName(_to, true, m_blockContext->hashHandler());
    auto table = m_tableFactory->openTable(tableName);
    if (!table)
    {
        EXECUTIVE_LOG(DEBUG) << LOG_DESC("depositFungibleAsset createAccount")
                             << LOG_KV("account", _to);
        m_s->setNonce(_to, u256(0));
        table = m_tableFactory->openTable(tableName);
    }
    auto entry = table->getRow(_assetName);
    if (!entry)
    {
        auto entry = table->newEntry();
        entry->setField("key", _assetName);
        entry->setField("value", to_string(_amount));
        table->setRow(_assetName, entry);
        return;
    }

    auto value = boost::lexical_cast<uint64_t>(entry->getField("value"));
    value += _amount;
    auto updateEntry = table->newEntry();
    updateEntry->setField("value", to_string(value));
    table->setRow(_assetName, updateEntry);
}

void HostContext::depositNotFungibleAsset(const std::string_view& _to,
    const std::string& _assetName, uint64_t _assetID, const std::string& _uri)
{
    auto tableName = getContractTableName(_to, true, m_blockContext->hashHandler());
    auto table = m_tableFactory->openTable(tableName);
    if (!table)
    {
        EXECUTIVE_LOG(DEBUG) << LOG_DESC("depositNotFungibleAsset createAccount")
                             << LOG_KV("account", _to);
        m_s->setNonce(_to, u256(0));
        table = m_tableFactory->openTable(tableName);
    }
    auto entry = table->getRow(_assetName);
    if (!entry)
    {
        auto entry = table->newEntry();
        entry->setField("value", to_string(_assetID));
        entry->setField("key", _assetName);
        table->setRow(_assetName, entry);
    }
    else
    {
        auto assetIDs = entry->getField("value");
        if (assetIDs.empty())
        {
            assetIDs = to_string(_assetID);
        }
        else
        {
            assetIDs = assetIDs + "," + to_string(_assetID);
        }
        auto updateEntry = table->newEntry();
        updateEntry->setField("key", _assetName);
        updateEntry->setField("value", assetIDs);
        table->setRow(_assetName, updateEntry);
    }
    entry = table->newEntry();
    auto key = _assetName + "-" + to_string(_assetID);
    entry->setField("key", key);
    entry->setField("value", _uri);
    table->setRow(key, entry);
}

bool HostContext::transferAsset(const std::string_view& _to, const std::string& _assetName,
    uint64_t _amountOrID, bool _fromSelf)
{
    // get asset info
    auto table = m_tableFactory->openTable(SYS_ASSET_INFO);
    auto assetEntry = table->getRow(_assetName);
    if (!assetEntry)
    {
        EXECUTIVE_LOG(WARNING) << "transferAsset " << _assetName << " is not exist";
        return false;
    }
    auto fungible = boost::lexical_cast<bool>(assetEntry->getField(SYS_ASSET_FUNGIBLE));
    auto from = caller();
    if (_fromSelf)
    {
        from = myAddress();
    }
    auto tableName = getContractTableName(from, true, m_blockContext->hashHandler());
    table = m_tableFactory->openTable(tableName);
    auto entry = table->getRow(_assetName);
    if (!entry)
    {
        EXECUTIVE_LOG(WARNING) << LOG_DESC("transferAsset account does not have")
                               << LOG_KV("asset", _assetName) << LOG_KV("account", from);
        return false;
    }
    EXECUTIVE_LOG(DEBUG) << LOG_DESC("transferAsset") << LOG_KV("asset", _assetName)
                         << LOG_KV("fungible", fungible) << LOG_KV("account", from);
    try
    {
        if (fungible)
        {
            auto value = boost::lexical_cast<uint64_t>(entry->getField("value"));
            value -= _amountOrID;
            auto updateEntry = table->newEntry();
            updateEntry->setField("key", _assetName);
            updateEntry->setField("value", to_string(value));
            table->setRow(_assetName, updateEntry);
            depositFungibleAsset(_to, _assetName, _amountOrID);
        }
        else
        {
            // TODO: check if from has asset

            auto tokenIDs = entry->getField("value");
            // find id in tokenIDs
            auto tokenID = to_string(_amountOrID);
            vector<string> tokenIDList;
            boost::split(tokenIDList, tokenIDs, boost::is_any_of(","));
            auto it = find(tokenIDList.begin(), tokenIDList.end(), tokenID);
            if (it != tokenIDList.end())
            {
                tokenIDList.erase(it);
                auto updateEntry = table->newEntry();
                updateEntry->setField("value", boost::algorithm::join(tokenIDList, ","));
                table->setRow(_assetName, updateEntry);
                auto tokenKey = _assetName + "-" + tokenID;
                auto entry = table->getRow(tokenKey);
                auto tokenURI = entry->getField("value");
                table->remove(tokenKey);
                depositNotFungibleAsset(_to, _assetName, _amountOrID, tokenURI);
            }
            else
            {
                EXECUTIVE_LOG(WARNING) << LOG_DESC("transferAsset account does not have")
                                       << LOG_KV("asset", _assetName) << LOG_KV("account", from);
                return false;
            }
        }
    }
    catch (std::exception& e)
    {
        EXECUTIVE_LOG(ERROR) << "transferAsset exception" << LOG_KV("what", e.what());
        return false;
    }

    return true;
}

uint64_t HostContext::getAssetBanlance(
    const std::string_view& _account, const std::string& _assetName)
{
    auto table = m_tableFactory->openTable(SYS_ASSET_INFO);
    auto assetEntry = table->getRow(_assetName);
    if (!assetEntry)
    {
        EXECUTIVE_LOG(WARNING) << "getAssetBanlance " << _assetName << " is not exist";
        return false;
    }
    auto fungible = boost::lexical_cast<bool>(assetEntry->getField(SYS_ASSET_FUNGIBLE));
    auto tableName = getContractTableName(_account, true, m_blockContext->hashHandler());
    table = m_tableFactory->openTable(tableName);
    if (!table)
    {
        return 0;
    }
    auto entry = table->getRow(_assetName);
    if (!entry)
    {
        return 0;
    }

    if (fungible)
    {
        return boost::lexical_cast<uint64_t>(entry->getField("value"));
    }
    // not fungible
    auto tokenIDS = entry->getField("value");
    uint64_t counts = std::count(tokenIDS.begin(), tokenIDS.end(), ',') + 1;
    return counts;
}

std::string HostContext::getNotFungibleAssetInfo(
    const std::string_view& _owner, const std::string& _assetName, uint64_t _assetID)
{
    auto tableName = getContractTableName(_owner, true, m_blockContext->hashHandler());
    auto table = m_tableFactory->openTable(tableName);
    if (!table)
    {
        EXECUTIVE_LOG(WARNING) << "getNotFungibleAssetInfo failed, account not exist"
                               << LOG_KV("account", _owner);
        return "";
    }
    auto assetKey = _assetName + "-" + to_string(_assetID);
    auto entry = table->getRow(assetKey);
    if (!entry)
    {
        EXECUTIVE_LOG(WARNING) << "getNotFungibleAssetInfo failed" << LOG_KV("account", _owner)
                               << LOG_KV("asset", assetKey);
        return "";
    }

    EXECUTIVE_LOG(DEBUG) << "getNotFungibleAssetInfo" << LOG_KV("account", _owner)
                         << LOG_KV("asset", _assetName) << LOG_KV("uri", entry->getField("value"));
    return entry->getField("value");
}

std::vector<uint64_t> HostContext::getNotFungibleAssetIDs(
    const std::string_view& _account, const std::string& _assetName)
{
    auto tableName = getContractTableName(_account, true, m_blockContext->hashHandler());
    auto table = m_tableFactory->openTable(tableName);
    if (!table)
    {
        EXECUTIVE_LOG(WARNING) << "getNotFungibleAssetIDs account not exist"
                               << LOG_KV("account", _account);
        return vector<uint64_t>();
    }
    auto entry = table->getRow(_assetName);
    if (!entry)
    {
        EXECUTIVE_LOG(WARNING) << "getNotFungibleAssetIDs account has none asset"
                               << LOG_KV("account", _account) << LOG_KV("asset", _assetName);
        return vector<uint64_t>();
    }

    auto tokenIDs = entry->getField("value");
    if (tokenIDs.empty())
    {
        EXECUTIVE_LOG(WARNING) << "getNotFungibleAssetIDs account has none asset"
                               << LOG_KV("account", _account) << LOG_KV("asset", _assetName);
        return vector<uint64_t>();
    }
    vector<string> tokenIDList;
    boost::split(tokenIDList, tokenIDs, boost::is_any_of(","));
    vector<uint64_t> ret(tokenIDList.size(), 0);
    EXECUTIVE_LOG(DEBUG) << "getNotFungibleAssetIDs" << LOG_KV("account", _account)
                         << LOG_KV("asset", _assetName) << LOG_KV("tokenIDs", tokenIDs);
    for (size_t i = 0; i < tokenIDList.size(); ++i)
    {
        ret[i] = boost::lexical_cast<uint64_t>(tokenIDList[i]);
    }
    return ret;
}
}  // namespace executor
}  // namespace bcos