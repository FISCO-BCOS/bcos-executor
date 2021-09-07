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
 */

#if 0
#pragma once
#include "bcos-framework/interfaces/crypto/Hash.h"
#include "StateInterface.h"
#include "bcos-framework/libutilities/Exceptions.h"
#include <string>

namespace bcos
{
namespace storage
{
class Table;
class StateStorage;
}  // namespace storage
namespace executor
{
    DERIVE_BCOS_EXCEPTION(NotEnoughCash);

const char* const STORAGE_KEY = "key";
const char* const STORAGE_VALUE = "value";
const char* const ACCOUNT_BALANCE = "balance";
const char* const ACCOUNT_CODE_HASH = "codeHash";
const char* const ACCOUNT_CODE = "code";
const char* const ACCOUNT_ABI = "abi";
const char* const ACCOUNT_NONCE = "nonce";
const char* const ACCOUNT_ALIVE = "alive";
const char* const ACCOUNT_AUTHORITY = "authority";
const char* const ACCOUNT_FROZEN = "frozen";

class State : public executor::StateInterface
{
public:
    explicit State(
        std::shared_ptr<storage::StateStorage> _tableFactory, crypto::Hash::Ptr _hashImpl, bool _isWasm)
      : m_tableFactory(_tableFactory), m_hashImpl(_hashImpl), m_isWasm(_isWasm){};
    virtual ~State() = default;
    /// Check if the address is in use.
    bool addressInUse(const std::string_view& _address) const override;

    /// Check if the account exists in the state and is non empty (nonce > 0 || balance > 0 || code
    /// nonempty and suiside != 1). These two notions are equivalent after EIP158.
    bool accountNonemptyAndExisting(const std::string_view& _address) const override;

    /// Check if the address contains executable code.
    bool addressHasCode(const std::string_view& _address) const override;

    /// Get an account's balance.
    /// @returns 0 if the address has never been used.
    u256 balance(const std::string_view& _address) const override;

    /// Add some amount to balance.
    /// Will initialise the address if it has never been used.
    void addBalance(const std::string_view& _address, u256 const& _amount) override;

    /// Subtract the @p _value amount from the balance of @p _address account.
    /// @throws NotEnoughCash if the balance of the account is less than the
    /// amount to be subtrackted (also in case the account does not exist).
    void subBalance(const std::string_view& _address, u256 const& _value) override;

    /// Set the balance of @p _address to @p _value.
    /// Will instantiate the address if it has never been used.
    void setBalance(const std::string_view& _address, u256 const& _value) override;

    /**
     * @brief Transfers "the balance @a _value between two accounts.
     * @param _from Account from which @a _value will be deducted.
     * @param _to Account to which @a _value will be added.
     * @param _value Amount to be transferred.
     */
    void transferBalance(const std::string_view& _from, const std::string_view& _to, u256 const& _value) override;

    /// Get the root of the storage of an account.
    crypto::HashType storageRoot(const std::string_view& _address) const override;

    /// Get the value of a storage position of an account.
    /// @returns 0 if no account exists at that address.
    u256 storage(const std::string_view& _address, const std::string_view& _memory) override;

    /// Set the value of a storage position of an account.
    void setStorage(const std::string_view& _address, const std::string_view& _location, const std::string_view& _value) override;

    /// Clear the storage root hash of an account to the hash of the empty trie.
    void clearStorage(const std::string_view& _address) override;

    /// Create a contract at the given address (with unset code and unchanged balance).
    void createContract(const std::string_view& _address) override;

    /// Sets the code of the account. Must only be called during / after contract creation.
    void setCode(const std::string_view& _address, bytesConstRef _code) override;

    /// Sets the ABI of the contract. Must only be called during / after contract creation.
    void setAbi(const std::string_view& _address, const std::string_view& _abi) override;

    /// Delete an account (used for processing suicides). (set suicides key = 1 when use AMDB)
    void kill(const std::string_view& _address) override;

    /// Get the storage of an account.
    /// @note This is expensive. Don't use it unless you need to.
    /// @returns map of hashed keys to key-value pairs or empty map if no account exists at that
    /// address.
    // virtual std::map<crypto::HashType, std::pair<u256, u256>> storage(const std::string_view& _address) const
    // override;

    /// Get the code of an account.
    /// @returns bytes() if no account exists at that address.
    /// @warning The reference to the code is only valid until the access to
    ///          other account. Do not keep it.
    std::shared_ptr<bytes> code(const std::string_view& _address) const override;

    /// Get the code hash of an account.
    /// @returns EmptyHash if no account exists at that address or if there is no code
    /// associated with the address.
    crypto::HashType codeHash(const std::string_view& _address) const override;

    /// Get the frozen status of an account.
    /// @returns ture if the account is frozen.
    bool frozen(const std::string_view& _address) const override;

    /// Get the byte-size of the code of an account.
    /// @returns code(_address).size(), but utilizes CodeSizeHash.
    size_t codeSize(const std::string_view& _address) const override;

    /// Increament the account nonce.
    void incNonce(const std::string_view& _address) override;

    /// Set the account nonce.
    void setNonce(const std::string_view& _address, u256 const& _newNonce) override;

    /// Get the account nonce -- the number of transactions it has sent.
    /// @returns 0 if the address has never been used.
    u256 getNonce(const std::string_view& _address) const override;

    /// The hash of the root of our state tree.
    crypto::HashType rootHash() const override;

    /// Commit all changes waiting in the address cache to the DB.
    /// @param _commitBehaviour whether or not to remove empty accounts during commit.
    void commit() override;

    /// Get the account start nonce. May be required.
    u256 const& accountStartNonce() const override;
    // u256 const& requireAccountStartNonce() const override;
    // void noteAccountStartNonce(u256 const& _actual) override;

    /// Create a savepoint in the state changelog.	///
    /// @return The savepoint index that can be used in rollback() function.
    size_t savepoint() const override;

    /// Revert all recent changes up to the given @p _savepoint savepoint.
    void rollback(size_t _savepoint) override;

    /// Clear state's cache
    void clear() override;

    bool checkAuthority(const std::string& _origin, const std::string& _address) const override;

    void setMemoryTableFactory(std::shared_ptr<storage::StateStorage> _tableFactory)
    {
        m_tableFactory = _tableFactory;
    }

private:
    void createAccount(const std::string_view& _address, u256 const& _nonce, u256 const& _amount = u256(0));
    std::shared_ptr<storage::Table> getTable(const std::string_view& _address) const;
    u256 m_accountStartNonce = 0;
    std::shared_ptr<storage::StateStorage> m_tableFactory;
    crypto::Hash::Ptr m_hashImpl;
    bool m_isWasm;
};
}  // namespace executor
}  // namespace bcos
#endif
