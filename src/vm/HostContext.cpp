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
#include "../ChecksumAddress.h"
#include "BlockContext.h"
#include "EVMHostInterface.h"
#include "bcos-framework/interfaces/storage/Table.h"
#include "bcos-framework/libstorage/StateStorage.h"
#include "evmc/evmc.hpp"
#include "libutilities/Common.h"
#include <algorithm>
#include <boost/algorithm/string/split.hpp>
#include <boost/thread.hpp>
#include <exception>
#include <limits>
#include <sstream>
#include <vector>

using namespace std;
using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::protocol;

namespace // anonymous
{
static unsigned const c_depthLimit = 1024;

/// Upper bound of stack space needed by single CALL/CREATE execution. Set
/// experimentally.
static size_t const c_singleExecutionStackSize = 100 * 1024;

static const std::string SYS_ASSET_NAME = "name";
static const std::string SYS_ASSET_FUNGIBLE = "fungible";
static const std::string SYS_ASSET_TOTAL = "total";
static const std::string SYS_ASSET_SUPPLIED = "supplied";
static const std::string SYS_ASSET_ISSUER = "issuer";
static const std::string SYS_ASSET_DESCRIPTION = "description";
static const std::string SYS_ASSET_INFO = "_sys_asset_info_";

const char STORAGE_KEY[] = "key";
const char STORAGE_VALUE[] = "value";
const char ACCOUNT_BALANCE[] = "balance";
const char ACCOUNT_CODE_HASH[] = "codeHash";
const char ACCOUNT_CODE[] = "code";
const char ACCOUNT_ABI[] = "abi";
const char ACCOUNT_NONCE[] = "nonce";
const char ACCOUNT_ALIVE[] = "alive";
const char ACCOUNT_AUTHORITY[] = "authority";
const char ACCOUNT_FROZEN[] = "frozen";

} // anonymous namespace

namespace bcos {
namespace executor {
namespace {

evmc_gas_metrics ethMetrics{32000, 20000, 5000, 200, 9000, 2300, 25000};

crypto::Hash::Ptr g_hashImpl = nullptr;

evmc_bytes32 evm_hash_fn(const uint8_t *data, size_t size) {
  return toEvmC(g_hashImpl->hash(bytesConstRef(data, size)));
}
} // namespace

HostContext::HostContext(std::shared_ptr<BlockContext> _blockContext,
                         int64_t _contextID, bcos::storage::Table table,
                         std::string contractAddress, std::string _caller,
                         std::string _origin, bytesConstRef _data,
                         unsigned _depth, bool _isCreate, bool _staticCall)
    : m_blockContext(std::move(_blockContext)), m_contextID(_contextID),
      m_table(std::move(table)), m_myAddress(std::move(contractAddress)),
      m_caller(std::move(_caller)), m_origin(std::move(_origin)), m_data(_data),
      m_depth(_depth), m_isCreate(_isCreate), m_staticCall(_staticCall) {

  interface = getHostInterface();
  wasm_interface = getWasmHostInterface();
  g_hashImpl = m_blockContext->hashHandler();

  hash_fn = evm_hash_fn;
  version = 0x03000000;
  isSMCrypto = false;

  if (g_hashImpl->getHashImplType() == crypto::HashImplType::Sm3Hash) {
    isSMCrypto = true;
  }
  metrics = &ethMetrics;
}

std::string_view HostContext::get(const std::string_view &_key) {
  auto entry = m_table.getRow(_key);
  if (entry) {
    auto it = m_key2Version.find(_key);
    if (it != m_key2Version.end()) {
      it->second = entry->version();
    } else {
      m_key2Version.emplace(_key, entry->version());
    }

    return entry->getField(STORAGE_VALUE);
  }

  return std::string_view();
}

void HostContext::set(const std::string_view &_key, std::string _value) {
  auto entry = m_table.newEntry();
  entry.importFields({std::move(_value)});

  auto it = m_key2Version.find(_key);
  if (it != m_key2Version.end()) {
    entry.setVersion(++it->second);
  }

  m_table.setRow(_key, std::move(entry));
}

evmc_result HostContext::call(CallParameters &_p) {
  // get last executive from blockcontext
  auto executive = m_blockContext->getLastExecutiveOf(m_contextID, m_myAddress);
  executive->setCallCreate(false);
  // create callResult use factory from blockcontext and call executive
  // returnCallback
  return executive->waitReturnValue(
      nullptr, m_blockContext->createExecutionResult(m_contextID, _p));
}

void HostContext::setCode(bytes code) {
  auto codeHashEntry = m_table.newEntry();
  auto codeHash = m_blockContext->hashHandler()->hash(code);
  codeHashEntry.importFields({codeHash.asBytes()});
  m_table.setRow(ACCOUNT_CODE_HASH, std::move(codeHashEntry));

  auto codeEntry = m_table.newEntry();
  codeEntry.importFields({std::move(code)});
  m_table.setRow(ACCOUNT_CODE, std::move(codeEntry));
}

size_t HostContext::codeSizeAt(const std::string_view &_a) {
  return 10 * 1024; // 10k code size ok?
}

h256 HostContext::codeHashAt(const std::string_view &_a) {
  return h256("0x1234567"); // ok?
}

u256 HostContext::store(const u256 &_n) {
  auto key = _n.str();

  auto entry = m_table.getRow(key);
  if (entry) {
    auto it = m_key2Version.find(key);
    if (it != m_key2Version.end()) {
      it->second = entry->version();
    } else {
      m_key2Version.emplace(key, entry->version());
    }

    return u256(entry->getField(STORAGE_VALUE));
  }

  return u256();
}

void HostContext::setStore(u256 const &_n, u256 const &_v) {
  auto key = _n.str();

  auto entry = m_table.newEntry();
  entry.importFields({_v.str()});

  auto it = m_key2Version.find(key);
  if (it != m_key2Version.end()) {
    entry.setVersion(++it->second);
  }

  m_table.setRow(key, std::move(entry));
}

evmc_result HostContext::create(int64_t io_gas, bytesConstRef _code,
                                evmc_opcode _op, u256 _salt) {
  if (m_blockContext->isWasm()) { // TODO: if wasm support contract create
                                  // contract add a new branch
    EXECUTIVE_LOG(ERROR) << "wasm contract new contract isn't supported";
    return evmc_result();
  }
  // get last executive from blockcontext
  auto executive = m_blockContext->getLastExecutiveOf(m_contextID, m_myAddress);
  executive->setCallCreate(true);
  // call executive returnCallback
  std::optional<u256> salt;
  if (_op != evmc_opcode::OP_CREATE) {
    assert(_op == evmc_opcode::OP_CREATE2);
    salt = _salt;
  }
  // create callResult use factory from blockcontext, schedule should make depth
  // + 1
  return executive->waitReturnValue(
      nullptr,
      m_blockContext->createExecutionResult(m_contextID, io_gas, _code, salt));
}

void HostContext::log(h256s &&_topics, bytesConstRef _data) {
  if (m_blockContext->isWasm() || m_myAddress.empty()) {
    m_sub.logs->push_back(protocol::LogEntry(
        bytes(m_myAddress.data(), m_myAddress.data() + m_myAddress.size()),
        std::move(_topics), _data.toBytes()));
  } else {
    // convert solidity address to hex string
    auto hexAddress = *toHexString(m_myAddress);
    boost::algorithm::to_lower(
        hexAddress); // this is in case of toHexString be modified
    toChecksumAddress(hexAddress,
                      m_blockContext->hashHandler()->hash(hexAddress).hex());
    m_sub.logs->push_back(protocol::LogEntry(
        asBytes(hexAddress), std::move(_topics), _data.toBytes()));
  }
}

void HostContext::suicide(const std::string_view &_a) {
  // m_sub.suicides.insert(m_myAddress);
  // m_s->kill(m_myAddress);
}

bytesConstRef HostContext::code() {
  auto entry = m_table.getRow(ACCOUNT_CODE);
  if (entry) {
    auto code = entry->getField(0);

    return bytesConstRef((bcos::byte *)code.data(), code.size());
  }

  return bytesConstRef();
}

h256 HostContext::codeHash() {
  auto entry = m_table.getRow(ACCOUNT_CODE_HASH);
  if (entry) {
    auto code = entry->getField(0);

    return h256(
        std::string(code)); // TODO: h256 support decode from string_view
  }

  return h256();
}

h256 HostContext::blockHash(int64_t _number) {
  return getBlockContext()->numberHash(_number);
}

bool HostContext::registerAsset(const std::string &_assetName,
                                const std::string_view &_addr, bool _fungible,
                                uint64_t _total,
                                const std::string &_description) {
  (void)_assetName;
  (void)_addr;
  (void)_fungible;
  (void)_total;
  (void)_description;

  return true;
  // auto table = m_tableFactory->openTable(SYS_ASSET_INFO);
  // if (table->getRow(_assetName)) {
  //   return false;
  // }
  // auto entry = table->newEntry();
  // entry.setField(SYS_ASSET_NAME, _assetName);
  // entry.setField(SYS_ASSET_ISSUER, string(_addr));
  // entry.setField(SYS_ASSET_FUNGIBLE, to_string(_fungible));
  // entry.setField(SYS_ASSET_TOTAL, to_string(_total));
  // entry.setField(SYS_ASSET_SUPPLIED, "0");
  // entry.setField(SYS_ASSET_DESCRIPTION, _description);
  // auto count = table->setRow(_assetName, entry);
  // return count == 1;
}

bool HostContext::issueFungibleAsset(const std::string_view &_to,
                                     const std::string &_assetName,
                                     uint64_t _amount) {
  (void)_to;
  (void)_assetName;
  (void)_amount;

  return true;
  // auto table = m_tableFactory->openTable(SYS_ASSET_INFO);
  // auto entry = table->getRow(_assetName);
  // if (!entry) {
  //   EXECUTIVE_LOG(WARNING) << "issueFungibleAsset " << _assetName
  //                          << "is not exist";
  //   return false;
  // }

  // auto issuer = std::string(entry->getField(SYS_ASSET_ISSUER));
  // if (caller() != issuer) {
  //   EXECUTIVE_LOG(WARNING) << "issueFungibleAsset not issuer of " <<
  //   _assetName
  //                          << LOG_KV("issuer", issuer)
  //                          << LOG_KV("caller", caller());
  //   return false;
  // }
  // // TODO: check supplied is less than total_supply
  // auto total =
  // boost::lexical_cast<uint64_t>(entry->getField(SYS_ASSET_TOTAL)); auto
  // supplied =
  //     boost::lexical_cast<uint64_t>(entry->getField(SYS_ASSET_SUPPLIED));
  // if (total - supplied < _amount) {
  //   EXECUTIVE_LOG(WARNING) << "issueFungibleAsset overflow total supply"
  //                          << LOG_KV("amount", _amount)
  //                          << LOG_KV("supplied", supplied)
  //                          << LOG_KV("total", total);
  //   return false;
  // }
  // // TODO: update supplied
  // auto updateEntry = table->newEntry();
  // updateEntry.setField(SYS_ASSET_SUPPLIED, to_string(supplied + _amount));
  // table->setRow(_assetName, updateEntry);
  // // TODO: create new tokens
  // depositFungibleAsset(_to, _assetName, _amount);
  // return true;
}

uint64_t HostContext::issueNotFungibleAsset(const std::string_view &_to,
                                            const std::string &_assetName,
                                            const std::string &_uri) {
  (void)_to;
  (void)_assetName;
  (void)_uri;
  return 0;
  // // check issuer
  // auto table = m_tableFactory->openTable(SYS_ASSET_INFO);
  // auto entry = table->getRow(_assetName);
  // if (!entry) {
  //   EXECUTIVE_LOG(WARNING) << "issueNotFungibleAsset " << _assetName
  //                          << "is not exist";
  //   return false;
  // }

  // auto issuer = std::string(entry->getField(SYS_ASSET_ISSUER));
  // if (caller() != issuer) {
  //   EXECUTIVE_LOG(WARNING) << "issueNotFungibleAsset not issuer of "
  //                          << _assetName;
  //   return false;
  // }
  // // check supplied
  // auto total =
  // boost::lexical_cast<uint64_t>(entry->getField(SYS_ASSET_TOTAL)); auto
  // supplied =
  //     boost::lexical_cast<uint64_t>(entry->getField(SYS_ASSET_SUPPLIED));
  // if (total - supplied == 0) {
  //   EXECUTIVE_LOG(WARNING) << "issueNotFungibleAsset overflow total supply"
  //                          << LOG_KV("supplied", supplied)
  //                          << LOG_KV("total", total);
  //   return false;
  // }
  // // get asset id and update supplied
  // auto assetID = supplied + 1;
  // auto updateEntry = table->newEntry();
  // updateEntry.setField(SYS_ASSET_SUPPLIED, to_string(assetID));
  // table->setRow(_assetName, updateEntry);

  // // create new tokens
  // depositNotFungibleAsset(_to, _assetName, assetID, _uri);
  // return assetID;
}

void HostContext::depositFungibleAsset(const std::string_view &_to,
                                       const std::string &_assetName,
                                       uint64_t _amount) {
  (void)_to;
  (void)_assetName;
  (void)_amount;
  // auto tableName =
  //     getContractTableName(_to, true, m_blockContext->hashHandler());
  // auto table = m_tableFactory->openTable(tableName);
  // if (!table) {
  //   EXECUTIVE_LOG(DEBUG) << LOG_DESC("depositFungibleAsset createAccount")
  //                        << LOG_KV("account", _to);
  //   m_s->setNonce(_to, u256(0));
  //   table = m_tableFactory->openTable(tableName);
  // }
  // auto entry = table->getRow(_assetName);
  // if (!entry) {
  //   auto entry = table->newEntry();
  //   entry.setField("key", _assetName);
  //   entry.setField("value", to_string(_amount));
  //   table->setRow(_assetName, entry);
  //   return;
  // }

  // auto value = boost::lexical_cast<uint64_t>(entry->getField("value"));
  // value += _amount;
  // auto updateEntry = table->newEntry();
  // updateEntry.setField("value", to_string(value));
  // table->setRow(_assetName, updateEntry);
}

void HostContext::depositNotFungibleAsset(const std::string_view &_to,
                                          const std::string &_assetName,
                                          uint64_t _assetID,
                                          const std::string &_uri) {
  (void)_to;
  (void)_assetName;
  (void)_assetID;
  (void)_uri;

  // auto tableName =
  //     getContractTableName(_to, true, m_blockContext->hashHandler());
  // auto table = m_tableFactory->openTable(tableName);
  // if (!table) {
  //   EXECUTIVE_LOG(DEBUG) << LOG_DESC("depositNotFungibleAsset createAccount")
  //                        << LOG_KV("account", _to);
  //   m_s->setNonce(_to, u256(0));
  //   table = m_tableFactory->openTable(tableName);
  // }
  // auto entry = table->getRow(_assetName);
  // if (!entry) {
  //   auto entry = table->newEntry();
  //   entry.setField("value", to_string(_assetID));
  //   entry.setField("key", _assetName);
  //   table->setRow(_assetName, entry);
  // } else {
  //   auto assetIDs = string(entry->getField("value"));
  //   if (assetIDs.empty()) {
  //     assetIDs = to_string(_assetID);
  //   } else {
  //     assetIDs = assetIDs + "," + to_string(_assetID);
  //   }
  //   auto updateEntry = table->newEntry();
  //   updateEntry.setField("key", _assetName);
  //   updateEntry.setField("value", assetIDs);
  //   table->setRow(_assetName, updateEntry);
  // }
  // auto newEntry = table->newEntry();
  // auto key = _assetName + "-" + to_string(_assetID);
  // newEntry.setField("key", key);
  // newEntry.setField("value", _uri);
  // table->setRow(key, newEntry);
}

bool HostContext::transferAsset(const std::string_view &_to,
                                const std::string &_assetName,
                                uint64_t _amountOrID, bool _fromSelf) {
  (void)_to;
  (void)_assetName;
  (void)_amountOrID;
  (void)_fromSelf;
  return true;
  // // get asset info
  // auto table = m_tableFactory->openTable(SYS_ASSET_INFO);
  // auto assetEntry = table->getRow(_assetName);
  // if (!assetEntry) {
  //   EXECUTIVE_LOG(WARNING) << "transferAsset " << _assetName << " is not
  //   exist"; return false;
  // }
  // auto fungible =
  //     boost::lexical_cast<bool>(assetEntry->getField(SYS_ASSET_FUNGIBLE));
  // auto from = caller();
  // if (_fromSelf) {
  //   from = myAddress();
  // }
  // auto tableName =
  //     getContractTableName(from, true, m_blockContext->hashHandler());
  // table = m_tableFactory->openTable(tableName);
  // auto entry = table->getRow(_assetName);
  // if (!entry) {
  //   EXECUTIVE_LOG(WARNING) << LOG_DESC("transferAsset account does not have")
  //                          << LOG_KV("asset", _assetName)
  //                          << LOG_KV("account", from);
  //   return false;
  // }
  // EXECUTIVE_LOG(DEBUG) << LOG_DESC("transferAsset")
  //                      << LOG_KV("asset", _assetName)
  //                      << LOG_KV("fungible", fungible)
  //                      << LOG_KV("account", from);
  // try {
  //   if (fungible) {
  //     auto value = boost::lexical_cast<uint64_t>(entry->getField("value"));
  //     value -= _amountOrID;
  //     auto updateEntry = table->newEntry();
  //     updateEntry.setField("key", _assetName);
  //     updateEntry.setField("value", to_string(value));
  //     table->setRow(_assetName, updateEntry);
  //     depositFungibleAsset(_to, _assetName, _amountOrID);
  //   } else {
  //     // TODO: check if from has asset
  //     auto tokenIDs = entry->getField("value");
  //     // find id in tokenIDs
  //     auto tokenID = to_string(_amountOrID);
  //     vector<string> tokenIDList;
  //     boost::split(tokenIDList, tokenIDs, boost::is_any_of(","));
  //     auto it = find(tokenIDList.begin(), tokenIDList.end(), tokenID);
  //     if (it != tokenIDList.end()) {
  //       tokenIDList.erase(it);
  //       auto updateEntry = table->newEntry();
  //       updateEntry.setField("value", boost::algorithm::join(tokenIDList,
  //       ",")); table->setRow(_assetName, updateEntry); auto tokenKey =
  //       _assetName + "-" + tokenID; auto entry = table->getRow(tokenKey);
  //       auto tokenURI = string(entry->getField("value"));
  //       // FIXME: how to use remove
  //       // table->remove(tokenKey);
  //       depositNotFungibleAsset(_to, _assetName, _amountOrID, tokenURI);
  //     } else {
  //       EXECUTIVE_LOG(WARNING)
  //           << LOG_DESC("transferAsset account does not have")
  //           << LOG_KV("asset", _assetName) << LOG_KV("account", from);
  //       return false;
  //     }
  //   }
  // } catch (std::exception &e) {
  //   EXECUTIVE_LOG(ERROR) << "transferAsset exception"
  //                        << LOG_KV("what", e.what());
  //   return false;
  // }

  // return true;
}

uint64_t HostContext::getAssetBanlance(const std::string_view &_account,
                                       const std::string &_assetName) {
  (void)_account;
  (void)_assetName;
  return 0;
  // auto table = m_tableFactory->openTable(SYS_ASSET_INFO);
  // auto assetEntry = table->getRow(_assetName);
  // if (!assetEntry) {
  //   EXECUTIVE_LOG(WARNING) << "getAssetBanlance " << _assetName
  //                          << " is not exist";
  //   return false;
  // }
  // auto fungible =
  //     boost::lexical_cast<bool>(assetEntry->getField(SYS_ASSET_FUNGIBLE));
  // auto tableName =
  //     getContractTableName(_account, true, m_blockContext->hashHandler());
  // table = m_tableFactory->openTable(tableName);
  // if (!table) {
  //   return 0;
  // }
  // auto entry = table->getRow(_assetName);
  // if (!entry) {
  //   return 0;
  // }

  // if (fungible) {
  //   return boost::lexical_cast<uint64_t>(entry->getField("value"));
  // }
  // // not fungible
  // auto tokenIDS = entry->getField("value");
  // uint64_t counts = std::count(tokenIDS.begin(), tokenIDS.end(), ',') + 1;
  // return counts;
}

std::string HostContext::getNotFungibleAssetInfo(const std::string_view &_owner,
                                                 const std::string &_assetName,
                                                 uint64_t _assetID) {
  (void)_owner;
  (void)_assetName;
  (void)_assetID;
  return {};
  //   auto tableName =
  //       getContractTableName(_owner, true, m_blockContext->hashHandler());
  //   auto table = m_tableFactory->openTable(tableName);
  //   if (!table) {
  //     EXECUTIVE_LOG(WARNING)
  //         << "getNotFungibleAssetInfo failed, account not exist"
  //         << LOG_KV("account", _owner);
  //     return "";
  //   }
  //   auto assetKey = _assetName + "-" + to_string(_assetID);
  //   auto entry = table->getRow(assetKey);
  //   if (!entry) {
  //     EXECUTIVE_LOG(WARNING) << "getNotFungibleAssetInfo failed"
  //                            << LOG_KV("account", _owner)
  //                            << LOG_KV("asset", assetKey);
  //     return "";
  //   }

  //   EXECUTIVE_LOG(DEBUG) << "getNotFungibleAssetInfo" << LOG_KV("account",
  //   _owner)
  //                        << LOG_KV("asset", _assetName)
  //                        << LOG_KV("uri", entry->getField("value"));
  //   return string(entry->getField("value"));
  // }

  // std::vector<uint64_t>
  // HostContext::getNotFungibleAssetIDs(const std::string_view &_account,
  //                                     const std::string &_assetName) {
  //   auto tableName =
  //       getContractTableName(_account, true, m_blockContext->hashHandler());
  //   auto table = m_tableFactory->openTable(tableName);
  //   if (!table) {
  //     EXECUTIVE_LOG(WARNING) << "getNotFungibleAssetIDs account not exist"
  //                            << LOG_KV("account", _account);
  //     return vector<uint64_t>();
  //   }
  //   auto entry = table->getRow(_assetName);
  //   if (!entry) {
  //     EXECUTIVE_LOG(WARNING) << "getNotFungibleAssetIDs account has none
  //     asset"
  //                            << LOG_KV("account", _account)
  //                            << LOG_KV("asset", _assetName);
  //     return vector<uint64_t>();
  //   }

  //   auto tokenIDs = entry->getField("value");
  //   if (tokenIDs.empty()) {
  //     EXECUTIVE_LOG(WARNING) << "getNotFungibleAssetIDs account has none
  //     asset"
  //                            << LOG_KV("account", _account)
  //                            << LOG_KV("asset", _assetName);
  //     return vector<uint64_t>();
  //   }
  //   vector<string> tokenIDList;
  //   boost::split(tokenIDList, tokenIDs, boost::is_any_of(","));
  //   vector<uint64_t> ret(tokenIDList.size(), 0);
  //   EXECUTIVE_LOG(DEBUG) << "getNotFungibleAssetIDs"
  //                        << LOG_KV("account", _account)
  //                        << LOG_KV("asset", _assetName)
  //                        << LOG_KV("tokenIDs", tokenIDs);
  //   for (size_t i = 0; i < tokenIDList.size(); ++i) {
  //     ret[i] = boost::lexical_cast<uint64_t>(tokenIDList[i]);
  //   }
  //   return ret;
}
} // namespace executor
} // namespace bcos