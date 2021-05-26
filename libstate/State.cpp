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
 */

#include "State.h"
#include "bcos-framework/libtable/TableFactory.h"

using namespace std;
using namespace bcos::storage;


namespace bcos
{
namespace executor
{
bool State::addressInUse(const std::string_view& _address) const
{
    auto table = getTable(_address);
    if (table)
    {
        return true;
    }
    return false;
}

bool State::accountNonemptyAndExisting(const std::string_view& _address) const
{
    auto table = getTable(_address);
    if (table)
    {
        if (balance(_address) > u256(0) || codeHash(_address) != m_hashImpl->emptyHash() ||
            getNonce(_address) != m_accountStartNonce)
            return true;
    }
    return false;
}

bool State::addressHasCode(const std::string_view& _address) const
{
    auto table = getTable(_address);
    if (table)
    {
        auto entry = table->getRow(ACCOUNT_CODE);
        if (entry)
        {
            return entry->getFieldConst(STORAGE_VALUE).size() != 0;
        }
    }
    return false;
}

u256 State::balance(const std::string_view& _address) const
{
    auto table = getTable(_address);
    if (table)
    {
        auto entry = table->getRow(ACCOUNT_BALANCE);
        if (entry)
        {
            return u256(entry->getField(STORAGE_VALUE));
        }
    }
    return 0;
}

void State::addBalance(const std::string_view& _address, u256 const& _amount)
{
    if (_amount == 0)
    {
        return;
    }
    auto table = getTable(_address);
    if (table)
    {
        auto entry = table->getRow(ACCOUNT_BALANCE);
        if (entry)
        {
            auto balance = u256(entry->getField(STORAGE_VALUE));
            balance += _amount;
            Entry::Ptr updateEntry = table->newEntry();
            updateEntry->setField(STORAGE_VALUE, balance.str());
            table->setRow(ACCOUNT_BALANCE, updateEntry);
        }
    }
    else
    {
        createAccount(_address, m_accountStartNonce, _amount);
    }
}

void State::subBalance(const std::string_view& _address, u256 const& _amount)
{
    auto table = getTable(_address);
    if (table)
    {
        auto entry = table->getRow(ACCOUNT_BALANCE);
        if (entry)
        {
            auto balance = u256(entry->getField(STORAGE_VALUE));
            if (balance < _amount)
                BOOST_THROW_EXCEPTION(NotEnoughCash());
            balance -= _amount;
            Entry::Ptr updateEntry = table->newEntry();
            updateEntry->setField(STORAGE_VALUE, balance.str());
            table->setRow(ACCOUNT_BALANCE, updateEntry);
        }
    }
    else
    {
        BOOST_THROW_EXCEPTION(NotEnoughCash());
    }
}

void State::setBalance(const std::string_view& _address, u256 const& _amount)
{
    auto table = getTable(_address);
    if (table)
    {
        auto entry = table->getRow(ACCOUNT_BALANCE);
        if (entry)
        {
            auto balance = u256(entry->getField(STORAGE_VALUE));
            balance = _amount;
            Entry::Ptr updateEntry = table->newEntry();
            updateEntry->setField(STORAGE_VALUE, balance.str());
            table->setRow(ACCOUNT_BALANCE, updateEntry);
        }
    }
    else
    {
        createAccount(_address, m_accountStartNonce, _amount);
    }
}

void State::transferBalance(const std::string_view& _from, const std::string_view& _to, u256 const& _value)
{
    subBalance(_from, _value);
    addBalance(_to, _value);
}

crypto::HashType State::storageRoot(const std::string_view& _address) const
{
    auto table = getTable(_address);
    if (table)
    {
        return table->hash();
    }
    return crypto::HashType();
}

u256 State::storage(const std::string_view& _address, const std::string_view& _key)
{
    auto table = getTable(_address);
    if (table)
    {
        auto entry = table->getRow(string(_key));
        if (entry)
        {
            return u256(entry->getField(STORAGE_VALUE));
        }
    }
    return u256(0);
}

void State::setStorage(
    const std::string_view& _address, const std::string_view& _location, const std::string_view& _value)
{
    auto table = getTable(_address);
    if (table)
    {
        auto entry = table->newEntry();
        entry->setField(STORAGE_KEY, string(_location));
        entry->setField(STORAGE_VALUE, string(_value));
        table->setRow(string(_location), entry);
    }
}

void State::clearStorage(const std::string_view&) {}

void State::setCode(const std::string_view& _address, bytesConstRef _code)
{
    auto table = getTable(_address);
    if (table)
    {
        auto entry = table->newEntry();
        auto codeHash = m_hashImpl->hash(_code);
        entry->setField(string(STORAGE_VALUE), string((char*)codeHash.data(), codeHash.size));
        table->setRow(ACCOUNT_CODE_HASH, entry);
        entry = table->newEntry();
        entry->setField(STORAGE_VALUE, string((char*)_code.data(), _code.size()));
        table->setRow(ACCOUNT_CODE, entry);
    }
}

void State::kill(const string_view& _address)
{
    auto table = getTable(_address);
    if (table)
    {
        auto entry = table->newEntry();
        entry->setField(STORAGE_VALUE, m_accountStartNonce.str());
        table->setRow(ACCOUNT_NONCE, entry);

        entry = table->newEntry();
        entry->setField(STORAGE_VALUE, u256(0).str());
        table->setRow(ACCOUNT_BALANCE, entry);

        entry = table->newEntry();
        entry->setField(STORAGE_VALUE, "");
        table->setRow(ACCOUNT_CODE, entry);

        entry = table->newEntry();
        entry->setField(STORAGE_VALUE,
            string((char*)m_hashImpl->emptyHash().data(), m_hashImpl->emptyHash().size));
        table->setRow(ACCOUNT_CODE_HASH, entry);

        entry = table->newEntry();
        entry->setField(STORAGE_VALUE, "false");
        table->setRow(ACCOUNT_ALIVE, entry);
    }
    clear();
}

shared_ptr<bytes> State::code(const std::string_view& _address) const
{  // FIXME: return
    if (codeHash(_address) == m_hashImpl->emptyHash())
        return nullptr;
    auto table = getTable(_address);
    if (table)
    {
        auto entry = table->getRow(ACCOUNT_CODE);
        auto code = entry->getFieldConst(STORAGE_VALUE);
        if (entry)
        {
            return make_shared<bytes>((byte*)code.data(), (byte*)(code.data() + code.size()));
        }
    }
    return nullptr;
}

crypto::HashType State::codeHash(const std::string_view& _address) const
{
    auto table = getTable(_address);
    if (table)
    {
        auto entry = table->getRow(ACCOUNT_CODE_HASH);
        if (entry)
        {
            auto codeHash = entry->getFieldConst(STORAGE_VALUE);
            return crypto::HashType((byte*)codeHash.data(), codeHash.size());
        }
    }
    return m_hashImpl->emptyHash();
}

bool State::frozen(const std::string_view& _contract) const
{
    auto table = getTable(_contract);
    if (table)
    {
        auto entry = table->getRow(ACCOUNT_FROZEN);
        if (entry)
        {
            return (entry->getField(STORAGE_VALUE) == "true");
        }
        else
        {
            return false;
        }
    }
    else
    {
        return false;
    }
}

size_t State::codeSize(const std::string_view& _address) const
{  // TODO: code should be cached
    return code(_address)->size();
}

void State::createContract(const std::string_view& _address)
{
    createAccount(_address, m_accountStartNonce);
}

void State::incNonce(const std::string_view& _address)
{
    auto table = getTable(_address);
    if (table)
    {
        auto entry = table->getRow(ACCOUNT_NONCE);
        if (entry)
        {
            auto nonce = u256(entry->getField(STORAGE_VALUE));
            ++nonce;
            Entry::Ptr updateEntry = table->newEntry();
            updateEntry->setField(STORAGE_VALUE, nonce.str());
            table->setRow(ACCOUNT_NONCE, updateEntry);
        }
    }
    else
        createAccount(_address, m_accountStartNonce + 1);
}

void State::setNonce(const std::string_view& _address, u256 const& _newNonce)
{
    auto table = getTable(_address);
    if (table)
    {
        auto entry = table->newEntry();
        entry->setField(STORAGE_VALUE, _newNonce.str());
        table->setRow(ACCOUNT_NONCE, entry);
    }
    else
        createAccount(_address, _newNonce);
}

u256 State::getNonce(const std::string_view& _address) const
{
    auto table = getTable(_address);
    if (table)
    {
        auto entry = table->getRow(ACCOUNT_NONCE);
        if (entry)
        {
            return u256(entry->getField(STORAGE_VALUE));
        }
    }
    return m_accountStartNonce;
}

crypto::HashType State::rootHash() const
{
    return m_tableFactory->hash();
}

void State::commit()
{
    m_tableFactory->commit();
}

u256 const& State::accountStartNonce() const
{
    return m_accountStartNonce;
}

size_t State::savepoint() const
{
    return m_tableFactory->savepoint();
}

void State::rollback(size_t _savepoint)
{
    m_tableFactory->rollback(_savepoint);
}

void State::clear()
{
    // m_cache.clear();
}

bool State::checkAuthority(const std::string& _origin, const std::string& _caller) const
{
    return m_tableFactory->checkAuthority(_origin, _caller);
}

void State::createAccount(const std::string_view& _address, u256 const& _nonce, u256 const& _amount)
{
    std::string tableName("c_" + string(_address));
    auto ret = m_tableFactory->createTable(tableName, STORAGE_KEY, STORAGE_VALUE);
    if (!ret)
    {
        LOG(ERROR) << LOG_BADGE("State") << LOG_DESC("createAccount failed")
                   << LOG_KV("Account", tableName);
        return;
    }
    auto table = m_tableFactory->openTable(tableName);
    auto entry = table->newEntry();
    entry->setField(STORAGE_KEY, ACCOUNT_BALANCE);
    entry->setField(STORAGE_VALUE, _amount.str());
    table->setRow(ACCOUNT_BALANCE, entry);

    entry = table->newEntry();
    entry->setField(STORAGE_KEY, ACCOUNT_CODE_HASH);
    entry->setField(
        STORAGE_VALUE, string((char*)m_hashImpl->emptyHash().data(), m_hashImpl->emptyHash().size));
    table->setRow(ACCOUNT_CODE_HASH, entry);

    entry = table->newEntry();
    entry->setField(STORAGE_KEY, ACCOUNT_CODE);
    entry->setField(STORAGE_VALUE, "");
    table->setRow(ACCOUNT_CODE, entry);

    entry = table->newEntry();
    entry->setField(STORAGE_KEY, ACCOUNT_NONCE);
    entry->setField(STORAGE_VALUE, _nonce.str());
    table->setRow(ACCOUNT_NONCE, entry);

    entry = table->newEntry();
    entry->setField(STORAGE_KEY, ACCOUNT_ALIVE);
    entry->setField(STORAGE_VALUE, "true");
    table->setRow(ACCOUNT_ALIVE, entry);
}

inline storage::TableInterface::Ptr State::getTable(const std::string_view& _address) const
{
    std::string tableName("c_" + string(_address));
    return m_tableFactory->openTable(tableName);
}
}  // namespace executor
}  // namespace bcos