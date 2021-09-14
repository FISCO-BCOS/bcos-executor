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
    HostContext(std::weak_ptr<TransactionExecutive> executive, bcos::storage::Table table,
        CallParameters callParams, unsigned _depth, bool _isCreate);
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
    evmc_result create(int64_t io_gas, bytesConstRef _code, evmc_opcode _op, u256 _salt);

    /// Create a new message call.
    evmc_result call(executor::CallParameters& _params);

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
    std::string_view myAddress() { return m_callParams.receiveAddress; }
    std::string_view caller() { return m_callParams.senderAddress; }
    std::string_view origin() { return m_callParams.origin; }
    bytesConstRef data() { return m_callParams.data; }
    bytesConstRef code();
    h256 codeHash();
    u256 const& salt() { return m_salt; }
    SubState& sub() { return m_sub; }
    unsigned const& depth() { return m_depth; }
    bool const& isCreate() { return m_isCreate; }
    bool const& staticCall() { return m_callParams.staticCall; }

private:
    void depositFungibleAsset(
        const std::string_view& _to, const std::string& _assetName, uint64_t _amount);
    void depositNotFungibleAsset(const std::string_view& _to, const std::string& _assetName,
        uint64_t _assetID, const std::string& _uri);

protected:
    std::weak_ptr<TransactionExecutive> m_executive;

private:
    bcos::storage::Table m_table;  ///< The table of contract
    CallParameters m_callParams;

    u256 m_salt;           ///< Values used in new address construction by CREATE2
    SubState m_sub;        ///< Sub-band VM state (suicides, refund counter, logs).
    unsigned m_depth = 0;  ///< Depth of the present call.

    std::map<std::string, size_t, std::less<>> m_key2Version;  // the version cache

    bool m_isCreate = false;  ///< Is this a CREATE call?
};

}  // namespace executor
}  // namespace bcos
