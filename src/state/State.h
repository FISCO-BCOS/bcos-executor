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
 * @brief state implement
 * @file State.h
 * @author: xingqiangbai
 * @date: 2021-05-20
 * @brief interface of Dispatcher
 * @file DispatcherInterface.h
 * @author: ancelmo
 * @date: 2021-09-09
 */

#pragma once
#include "bcos-framework/interfaces/crypto/Hash.h"
#include "bcos-framework/libutilities/Exceptions.h"
#include "interfaces/storage/StorageInterface.h"
#include "libstorage/StateStorage.h"
#include "libutilities/Common.h"
#include <string>

namespace bcos {
namespace executor {
DERIVE_BCOS_EXCEPTION(NotEnoughCash);

const char *const STORAGE_KEY = "key";
const char STORAGE_VALUE[] = "value";
const char *const ACCOUNT_BALANCE = "balance";
const char *const ACCOUNT_CODE_HASH = "codeHash";
const char *const ACCOUNT_CODE = "code";
const char *const ACCOUNT_ABI = "abi";
const char *const ACCOUNT_NONCE = "nonce";
const char *const ACCOUNT_ALIVE = "alive";
const char *const ACCOUNT_AUTHORITY = "authority";
const char *const ACCOUNT_FROZEN = "frozen";

class State {
public:
  explicit State(bcos::storage::Table table, crypto::Hash::Ptr _hashImpl,
                 bool _isWasm)
      : m_table(std::move(table)), m_hashImpl(std::move(_hashImpl)),
        m_isWasm(_isWasm){};

  virtual ~State() = default;
  /// Check if the address is in use.
  bool addressInUse(const std::string_view &_address) const;

  /// Check if the account exists in the state and is non empty (nonce > 0 ||
  /// balance > 0 || code nonempty and suiside != 1). These two notions are
  /// equivalent after EIP158.
  bool accountNonemptyAndExisting(const std::string_view &_address) const;

  /// Check if the address contains executable code.
  bool addressHasCode(const std::string_view &_address) const;

  /// Get the value of a storage position of an account.
  /// @returns 0 if no account exists at that address.
  std::string_view storage(const std::string_view &key);

  /// Set the value of a storage position of an account.
  void setStorage(const std::string_view &key, const std::string value);

  /// Clear the storage root hash of an account to the hash of the empty trie.
  void clearStorage();

  /// Sets the code of the account. Must only be called during / after contract
  /// creation.
  void setCode(bytes code);

  /// Sets the ABI of the contract. Must only be called during / after contract
  /// creation.
  void setABI(std::string abi);

  /// Delete an account (used for processing suicides). (set suicides key = 1
  /// when use AMDB)
  void kill();

  /// Get the code of an account.
  /// @returns bytes() if no account exists at that address.
  /// @warning The reference to the code is only valid until the access to
  ///          other account. Do not keep it.
  bytesConstRef code() const;

  /// Get the code hash of an account.
  /// @returns EmptyHash if no account exists at that address or if there is no
  /// code associated with the address.
  crypto::HashType codeHash() const;

  /// Get the frozen status of an account.
  /// @returns ture if the account is frozen.
  bool frozen() const;

  /// Get the byte-size of the code of an account.
  /// @returns code(_address).size(), but utilizes CodeSizeHash.
  size_t codeSize() const;

  /// Increament the account nonce.
  void incNonce();

  /// Set the account nonce.
  void setNonce(u256 const &_newNonce);

  /// Get the account nonce -- the number of transactions it has sent.
  /// @returns 0 if the address has never been used.
  u256 getNonce() const;

  /// Get the account start nonce. May be required.
  u256 const &accountStartNonce() const;

  /// Clear state's cache
  void clear();

private:
  mutable bcos::storage::Table m_table;
  crypto::Hash::Ptr m_hashImpl;

  std::map<std::string, size_t, std::less<>> m_key2Version;
  u256 m_accountStartNonce = 0;
  bool m_isWasm;
};
} // namespace executor
} // namespace bcos
