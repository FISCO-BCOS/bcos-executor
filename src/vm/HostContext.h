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

#include "Common.h"
#include "TransactionExecutive.h"
#include <evmc/evmc.h>
#include <evmc/helpers.h>
#include <evmc/instructions.h>
#include <functional>
#include <map>

namespace bcos
{
namespace executor
{
class HostContext : public evmc_host_context
{
public:
    /// Full constructor.
    HostContext(
        std::weak_ptr<TransactionExecutive> executive, bcos::storage::Table table, unsigned _depth);
    ~HostContext() = default;

    HostContext(HostContext const&) = delete;
    HostContext& operator=(HostContext const&) = delete;

    std::string_view get(const std::string_view& _key);

    void set(const std::string_view& _key, std::string _value);

    bool registerAsset(const std::string& _assetName, const std::string_view& _issuer,
        bool _fungible, uint64_t _total, const std::string& _description);
    bool issueFungibleAsset(
        const std::string_view& _to, const std::string& _assetName, uint64_t _amount);
    uint64_t issueNotFungibleAsset(
        const std::string_view& _to, const std::string& _assetName, const std::string& _uri);
    std::string getNotFungibleAssetInfo(
        const std::string_view& _owner, const std::string& _assetName, uint64_t _id);
    bool transferAsset(const std::string_view& _to, const std::string& _assetName,
        uint64_t _amountOrID, bool _fromSelf);

    // if NFT return counts, else return value
    uint64_t getAssetBanlance(const std::string_view& _account, const std::string& _assetName);

    std::vector<uint64_t> getNotFungibleAssetIDs(
        const std::string_view& _account, const std::string& _assetName);
    /// Read storage location.
    u256 store(const u256& _n);

    /// Write a value in storage.
    void setStore(const u256& _n, const u256& _v);

    /// Create a new contract.
    evmc_result externalCreate(CallParameters callParameters, std::optional<u256> _salt);

    /// Create a new message call.
    evmc_result externalCall(executor::CallParameters _params);

    void setCode(bytes code);

    size_t codeSizeAt(const std::string_view& _a);

    h256 codeHashAt(const std::string_view& _a);

    /// Does the account exist?
    bool exists(const std::string_view&) { return true; }

    /// Suicide the associated contract to the given address.
    void suicide();

    /// Return the EVM gas-price schedule for this execution context.
    EVMSchedule const& evmSchedule() const
    {
        return m_executive.lock()->blockContext()->evmSchedule();
    }

    /// Hash of a block if within the last 256 blocks, or h256() otherwise.
    h256 blockHash();

    bool isPermitted();

    /// Get the execution environment information.
    std::shared_ptr<BlockContext> getBlockContext() const
    {
        return m_executive.lock()->blockContext();
    }

    /// Revert any changes made (by any of the other calls).
    void log(h256s&& _topics, bytesConstRef _data);

    void suicide(const std::string_view& _a);

    /// ------ get interfaces related to HostContext------
    std::string_view myAddress() const { return m_executive.lock()->contractAddress(); }
    std::string_view caller() const { return m_executive.lock()->callParameters().senderAddress; }
    std::string_view origin() const { return m_executive.lock()->callParameters().origin; }
    bytesConstRef data() const { return m_executive.lock()->callParameters().data; }
    bytesConstRef code();
    h256 codeHash();
    u256 salt() const { return m_salt; }
    SubState& sub() { return m_sub; }
    unsigned depth() const { return m_depth; }
    bool isCreate() const { return m_executive.lock()->callCreate(); }
    bool staticCall() const { return m_executive.lock()->callParameters().staticCall; }
    int64_t gas() const { return m_executive.lock()->gas(); }

private:
    void depositFungibleAsset(
        const std::string_view& _to, const std::string& _assetName, uint64_t _amount);
    void depositNotFungibleAsset(const std::string_view& _to, const std::string& _assetName,
        uint64_t _assetID, const std::string& _uri);

protected:
    std::weak_ptr<TransactionExecutive> m_executive;

private:
    bcos::storage::Table m_table;  ///< The table of contract

    u256 m_salt;           ///< Values used in new address construction by CREATE2
    SubState m_sub;        ///< Sub-band VM state (suicides, refund counter, logs).
    unsigned m_depth = 0;  ///< Depth of the present call.

    std::map<std::string, size_t, std::less<>> m_key2Version;  // the version cache
};

}  // namespace executor
}  // namespace bcos
