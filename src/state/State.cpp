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
 * @brief interface of Dispatcher
 * @file DispatcherInterface.h
 * @author: xingqiangbai
 * @date: 2021-05-20
 * @brief interface of Dispatcher
 * @file DispatcherInterface.h
 * @author: ancelmo
 * @date: 2021-09-09
 */
#include "State.h"
#include "interfaces/crypto/CommonType.h"
#include <shared_mutex>

using namespace std;
using namespace bcos;
using namespace bcos::executor;

bool State::addressInUse(const std::string_view &) const { return true; }

bool State::accountNonemptyAndExisting(const std::string_view &) const {
  return true;
}

bool State::addressHasCode(const std::string_view &) const { return true; }

std::string_view State::storage(const std::string_view &key) {
  auto entry = m_table.getRow(key);
  if (entry) {
    auto it = m_key2Version.find(key);
    if (it != m_key2Version.end()) {
      it->second = entry->version();
    } else {
      m_key2Version.emplace(key, entry->version());
    }

    return entry->getField(STORAGE_VALUE);
  }

  return std::string_view();
}

void State::setStorage(const std::string_view &key, const std::string value) {
  auto entry = m_table.newEntry();
  entry.importFields({value});

  auto it = m_key2Version.find(key);
  if (it != m_key2Version.end()) {
    entry.setVersion(++it->second);
  }

  m_table.setRow(key, std::move(entry));
}

void State::clearStorage() { // do nothing
}

void State::setCode(bytes code) {
  auto codeHashEntry = m_table.newEntry();
  auto codeHash = m_hashImpl->hash(code);
  codeHashEntry.setField(STORAGE_VALUE, codeHash.asBytes());
  m_table.setRow(ACCOUNT_CODE_HASH, std::move(codeHashEntry));

  auto codeEntry = m_table.newEntry();
  codeEntry.setField(STORAGE_VALUE, std::move(code));
  m_table.setRow(ACCOUNT_CODE, std::move(codeEntry));
}

void State::setABI(std::string abi) {
  auto abiEntry = m_table.newEntry();
  abiEntry.setField(STORAGE_VALUE, std::move(abi));
  m_table.setRow(ACCOUNT_ABI, std::move(abiEntry));
}

void State::kill() {}

bytesConstRef State::code() const {
  auto entry = m_table.getRow(ACCOUNT_CODE);
  auto code = entry->getField(STORAGE_VALUE);
  if (entry) {
    return bytesConstRef((bcos::byte *)code.data(), code.size());
  } else {
    return bytesConstRef();
  }
}

crypto::HashType State::codeHash() const {
  auto entry = m_table.getRow(ACCOUNT_CODE_HASH);
  if (entry) {
    auto codeHash = entry->getField(STORAGE_VALUE);
    return crypto::HashType((byte *)codeHash.data(), codeHash.size());
  }
  return m_hashImpl->emptyHash();
}

bool State::frozen() const {}

size_t State::codeSize() const { // TODO: code should be cached
  auto entry = m_table.getRow(ACCOUNT_CODE);
  auto code = entry->getField(STORAGE_VALUE);
  return code.size();
}

void State::incNonce() {
  auto entry = m_table.getRow(ACCOUNT_NONCE);
  if (entry) {
    auto nonce = u256(entry->getField(STORAGE_VALUE));
    ++nonce;

    auto updateEntry = m_table.newEntry();
    updateEntry.setField(STORAGE_VALUE, nonce.str());
    updateEntry.setVersion(entry->version() + 1);
    m_table.setRow(ACCOUNT_NONCE, updateEntry);
  }
}

void State::setNonce(u256 const &_newNonce) {
  auto entry = m_table.newEntry();
  entry.setField(STORAGE_VALUE, _newNonce.str());
  m_table.setRow(ACCOUNT_NONCE, entry);
}

u256 State::getNonce() const {
  auto entry = m_table.getRow(ACCOUNT_NONCE);
  if (entry) {
    return u256(entry->getField(STORAGE_VALUE));
  }
  return m_accountStartNonce;
}

u256 const &State::accountStartNonce() const { return m_accountStartNonce; }

void State::clear() {
  // m_cache.clear();
}

// void State::createAccount(const std::string_view &_address, u256 const &_nonce,
//                           u256 const &_amount) {
//   std::string tableName = getContractTableName(_address, m_isWasm, m_hashImpl);
//   auto ret = m_tableFactory->createTable(tableName, STORAGE_KEY, STORAGE_VALUE);
//   if (!ret) {
//     BCOS_LOG(ERROR) << LOG_BADGE("State") << LOG_DESC("createAccount failed")
//                     << LOG_KV("Account", tableName);
//     return;
//   }
//   auto table = m_tableFactory->openTable(tableName);
//   auto entry = table->newEntry();
//   entry->setField(STORAGE_KEY, ACCOUNT_BALANCE);
//   entry->setField(STORAGE_VALUE, _amount.str());
//   table->setRow(ACCOUNT_BALANCE, entry);

//   entry = table->newEntry();
//   entry->setField(STORAGE_KEY, ACCOUNT_CODE_HASH);
//   entry->setField(STORAGE_VALUE, string((char *)m_hashImpl->emptyHash().data(),
//                                         m_hashImpl->emptyHash().size));
//   table->setRow(ACCOUNT_CODE_HASH, entry);

//   entry = table->newEntry();
//   entry->setField(STORAGE_KEY, ACCOUNT_CODE);
//   entry->setField(STORAGE_VALUE, "");
//   table->setRow(ACCOUNT_CODE, entry);

//   entry = table->newEntry();
//   entry->setField(STORAGE_KEY, ACCOUNT_NONCE);
//   entry->setField(STORAGE_VALUE, _nonce.str());
//   table->setRow(ACCOUNT_NONCE, entry);

//   entry = table->newEntry();
//   entry->setField(STORAGE_KEY, ACCOUNT_ALIVE);
//   entry->setField(STORAGE_VALUE, "true");
//   table->setRow(ACCOUNT_ALIVE, entry);
// }