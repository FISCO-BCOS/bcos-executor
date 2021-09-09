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
#include <string>

namespace bcos {
namespace storage {
class Table;
class StateStorage;
} // namespace storage
namespace executor {
DERIVE_BCOS_EXCEPTION(NotEnoughCash);

const char *const STORAGE_KEY = "key";
const char *const STORAGE_VALUE = "value";
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
  explicit State(const std::string_view &contractAddress,
                 storage::StateStorage::Ptr storage,
                 crypto::Hash::Ptr _hashImpl, bool _isWasm)
      : m_storage(std::move(storage)), m_hashImpl(std::move(_hashImpl)),
        m_table(*getTable(contractAddress)), m_isWasm(_isWasm){};

  virtual ~State() = default;
  /// Check if the address is in use.
  bool addressInUse(const std::string_view &_address) const;

  /// Check if the account exists in the state and is non empty (nonce > 0 ||
  /// balance > 0 || code nonempty and suiside != 1). These two notions are
  /// equivalent after EIP158.
  bool
  accountNonemptyAndExisting(const std::string_view &_address) const;

  /// Check if the address contains executable code.
  bool addressHasCode(const std::string_view &_address) const;

  /// Get an account's balance.
  /// @returns 0 if the address has never been used.
  u256 balance(const std::string_view &_address) const;

  /// Add some amount to balance.
  /// Will initialise the address if it has never been used.
  void addBalance(const std::string_view &_address,
                  u256 const &_amount);

  /// Subtract the @p _value amount from the balance of @p _address account.
  /// @throws NotEnoughCash if the balance of the account is less than the
  /// amount to be subtrackted (also in case the account does not exist).
  void subBalance(const std::string_view &_address,
                  u256 const &_value);

  /// Set the balance of @p _address to @p _value.
  /// Will instantiate the address if it has never been used.
  void setBalance(const std::string_view &_address,
                  u256 const &_value);

  /// Get the root of the storage of an account.
  crypto::HashType storageRoot(const std::string_view &_address) const;

  /// Get the value of a storage position of an account.
  /// @returns 0 if no account exists at that address.
  std::string storage(const std::string_view &_address,
                      const std::string_view &_memory);

  /// Set the value of a storage position of an account.
  void setStorage(const std::string_view &_address,
                  const std::string_view &_location,
                  const std::string_view &_value);

  /// Clear the storage root hash of an account to the hash of the empty trie.
  void clearStorage(const std::string_view &_address);

  /// Create a contract at the given address (with unset code and unchanged
  /// balance).
  void createContract(const std::string_view &_address);

  /// Sets the code of the account. Must only be called during / after contract
  /// creation.
  void setCode(const std::string_view &_address, bytesConstRef _code);

  /// Sets the ABI of the contract. Must only be called during / after contract
  /// creation.
  void setAbi(const std::string_view &_address,
              const std::string_view &_abi);

  /// Delete an account (used for processing suicides). (set suicides key = 1
  /// when use AMDB)
  void kill(const std::string_view &_address);

  /// Get the code of an account.
  /// @returns bytes() if no account exists at that address.
  /// @warning The reference to the code is only valid until the access to
  ///          other account. Do not keep it.
  std::shared_ptr<bytes> code(const std::string_view &_address) const;

  /// Get the code hash of an account.
  /// @returns EmptyHash if no account exists at that address or if there is no
  /// code associated with the address.
  crypto::HashType codeHash(const std::string_view &_address) const;

  /// Get the frozen status of an account.
  /// @returns ture if the account is frozen.
  bool frozen(const std::string_view &_address) const;

  /// Get the byte-size of the code of an account.
  /// @returns code(_address).size(), but utilizes CodeSizeHash.
  size_t codeSize(const std::string_view &_address) const;

  /// Increament the account nonce.
  void incNonce(const std::string_view &_address);

  /// Set the account nonce.
  void setNonce(const std::string_view &_address,
                u256 const &_newNonce);

  /// Get the account nonce -- the number of transactions it has sent.
  /// @returns 0 if the address has never been used.
  u256 getNonce(const std::string_view &_address) const;

  /// The hash of the root of our state tree.
  crypto::HashType rootHash() const;

  /// Get the account start nonce. May be required.
  u256 const &accountStartNonce() const;
  // u256 const& requireAccountStartNonce() const override;
  // void noteAccountStartNonce(u256 const& _actual) override;

  /// Create a savepoint in the state changelog.	///
  /// @return The savepoint index that can be used in rollback() function.
  size_t savepoint() const;

  /// Revert all recent changes up to the given @p _savepoint savepoint.
  void rollback(size_t _savepoint);

  /// Clear state's cache
  void clear();

  bool checkAuthority(const std::string &_origin,
                      const std::string &_address) const;

private:
  void createAccount(const std::string_view &_address, u256 const &_nonce,
                     u256 const &_amount = u256(0));
  std::optional<storage::Table>
  getTable(const std::string_view &_address) const;

  u256 m_accountStartNonce = 0;
  storage::StateStorage::Ptr m_storage;
  crypto::Hash::Ptr m_hashImpl;
  mutable bcos::storage::Table m_table;
  std::map<std::string, size_t> m_key2Version;
  bool m_isWasm;
};
} // namespace executor
} // namespace bcos
