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
 * @file HostContext.h
 * @author: xingqiangbai
 * @date: 2021-05-24
 */

#pragma once

#include "../libstate/StateInterface.h"
#include "Common.h"
#include "Executive.h"
#include <evmc/evmc.h>
#include <evmc/helpers.h>
#include <evmc/instructions.h>
#include <functional>
#include <map>

namespace bcos
{
namespace storage
{
class TableFactoryInterface;
}
namespace executor
{
class StateInterface;
/// Externality interface for the Virtual Machine providing access to world state.
class HostContext : public evmc_host_context
{
public:
    /// Full constructor.
    HostContext(std::shared_ptr<executor::StateInterface> _s, executor::EnvInfo const& _envInfo,
        const std::string_view& _myAddress, const std::string_view& _caller,
        const std::string_view& _origin, bytesConstRef _data, const std::shared_ptr<bytes>& _code, h256 const& _codeHash,
        unsigned _depth, bool _isCreate, bool _staticCall);
    virtual ~HostContext() = default;

    HostContext(HostContext const&) = delete;
    HostContext& operator=(HostContext const&) = delete;

    std::string get(const std::string_view& _key) { return m_s->storage(myAddress(), _key).str(); }
    void set(const std::string_view& _key, const std::string_view& _value)
    {
        m_s->setStorage(myAddress(), _key, _value);
    }
    virtual bool registerAsset(const std::string& _assetName, const std::string_view& _issuer,
        bool _fungible, uint64_t _total, const std::string& _description);
    virtual bool issueFungibleAsset(
        const std::string_view& _to, const std::string& _assetName, uint64_t _amount);
    virtual uint64_t issueNotFungibleAsset(
        const std::string_view& _to, const std::string& _assetName, const std::string& _uri);
    virtual std::string getNotFungibleAssetInfo(
        const std::string_view& _owner, const std::string& _assetName, uint64_t _id);
    bool transferAsset(const std::string_view& _to, const std::string& _assetName,
        uint64_t _amountOrID, bool _fromSelf);
    // if NFT return counts, else return value
    uint64_t getAssetBanlance(const std::string_view& _account, const std::string& _assetName);
    std::vector<uint64_t> getNotFungibleAssetIDs(
        const std::string_view& _account, const std::string& _assetName);
    /// Read storage location.
    virtual u256 store(const u256& _n) { return m_s->storage(myAddress(), _n.str()); }

    /// Write a value in storage.
    virtual void setStore(const u256& _n, const u256& _v);

    /// Read address's code.
    virtual std::shared_ptr<bytes> codeAt(const std::string_view& _a) { return m_s->code(_a); }

    /// @returns the size of the code in  bytes at the given address.
    virtual size_t codeSizeAt(const std::string_view& _a);

    /// @returns the hash of the code at the given address.
    virtual h256 codeHashAt(const std::string_view& _a);

    /// Create a new contract.
    virtual evmc_result create(u256& io_gas, bytesConstRef _code, evmc_opcode _op, u256 _salt);

    /// Create a new message call.
    virtual evmc_result call(executor::CallParameters& _params);

    /// Read address's balance.
    virtual u256 balance(const std::string_view& _a) { return m_s->balance(_a); }

    /// Does the account exist?
    virtual bool exists(const std::string_view& _a)
    {
        if (evmSchedule().emptinessIsNonexistence())
            return m_s->accountNonemptyAndExisting(_a);
        else
            return m_s->addressInUse(_a);
    }

    /// Suicide the associated contract to the given address.
    virtual void suicide(const std::string_view& _a);

    /// Return the EVM gas-price schedule for this execution context.
    virtual EVMSchedule const& evmSchedule() const { return m_envInfo.evmSchedule(); }

    virtual std::shared_ptr<executor::StateInterface> const& state() const { return m_s; }

    /// Hash of a block if within the last 256 blocks, or h256() otherwise.
    virtual h256 blockHash(int64_t _number);

    virtual bool isPermitted();

    /// Get the execution environment information.
    virtual EnvInfo const& envInfo() const { return m_envInfo; }

    /// Revert any changes made (by any of the other calls).
    virtual void log(h256s&& _topics, bytesConstRef _data)
    {
        m_sub.logs->push_back(
            protocol::LogEntry(bytes(m_myAddress.data(), m_myAddress.data() + m_myAddress.size()),
                std::move(_topics), _data.toBytes()));
    }

    /// ------ get interfaces related to HostContext------
    virtual const std::string& myAddress() { return m_myAddress; }
    virtual const std::string& caller() { return m_caller; }
    virtual const std::string& origin() { return m_origin; }
    virtual bytesConstRef const& data() { return m_data; }
    virtual std::shared_ptr<bytes> const& code() { return m_code; }
    virtual h256 const& codeHash() { return m_codeHash; }
    virtual u256 const& salt() { return m_salt; }
    virtual SubState& sub() { return m_sub; }
    virtual unsigned const& depth() { return m_depth; }
    virtual bool const& isCreate() { return m_isCreate; }
    virtual bool const& staticCall() { return m_staticCall; }

private:
    void depositFungibleAsset(
        const std::string_view& _to, const std::string& _assetName, uint64_t _amount);
    void depositNotFungibleAsset(const std::string_view& _to, const std::string& _assetName,
        uint64_t _assetID, const std::string& _uri);

protected:
    EnvInfo const& m_envInfo;

private:
    std::string m_myAddress;    ///< address associated with executing code (a contract, or
                                ///< contract-to-be).
    std::string m_caller;       ///< address which sent the message (either equal to origin or a
                                ///< contract).
    std::string m_origin;       ///< Original transactor.
    bytesConstRef m_data;       ///< Current input data.
    std::shared_ptr<bytes> m_code;               ///< Current code that is executing.
    h256 m_codeHash;            ///< Keccak256 hash of the executing code
    u256 m_salt;                ///< Values used in new address construction by CREATE2
    SubState m_sub;             ///< Sub-band VM state (suicides, refund counter, logs).
    unsigned m_depth = 0;       ///< Depth of the present call.
    bool m_isCreate = false;    ///< Is this a CREATE call?
    bool m_staticCall = false;  ///< Throw on state changing.

    std::shared_ptr<executor::StateInterface> m_s;  ///< A reference to the base state.
    std::shared_ptr<storage::TableFactoryInterface> m_tableFactory;
};

}  // namespace executor
}  // namespace bcos
