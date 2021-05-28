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
 */
/**
 * @brief : Generate transaction DAG for parallel execution
 * @author: jimmyshi
 * @date: 2019-1-8
 */

#pragma once
#include "../libvm/ExecutiveContext.h"
#include "DAG.h"
#include "bcos-framework/interfaces/protocol/Block.h"
#include "bcos-framework/interfaces/protocol/Transaction.h"
#include <map>
#include <memory>
#include <queue>
#include <vector>

namespace bcos
{
namespace executor
{
class Executive;
using ExecuteTxFunc =
    std::function<bool(protocol::Transaction::ConstPtr, ID, std::shared_ptr<Executive>)>;

class TxDAG
{
public:
    TxDAG() : m_dag() {}
    virtual ~TxDAG() {}

    // Generate DAG according with given transactions
    void init(ExecutiveContext::Ptr _ctx, const protocol::Block::Ptr& _block);

    // Set transaction execution function
    void setTxExecuteFunc(ExecuteTxFunc const& _f);

    // Called by thread
    // Has the DAG reach the end?
    // process-exit related:
    // if the m_stop is true(may be the storage has exceptioned), return true
    // directly
    bool hasFinished() { return (m_exeCnt >= m_totalParaTxs) || (m_stop.load()); }

    // Called by thread
    // Execute a unit in DAG
    // This function can be parallel
    int executeUnit(std::shared_ptr<Executive> _executive);

    ID paraTxsNumber() { return m_totalParaTxs; }

    ID haveExecuteNumber() { return m_exeCnt; }
    void stop() { m_stop.store(true); }

private:
    ExecuteTxFunc f_executeTx;
    protocol::Block::Ptr m_block;
    DAG m_dag;

    ID m_exeCnt = 0;
    ID m_totalParaTxs = 0;

    mutable std::mutex x_exeCnt;
    std::atomic_bool m_stop = {false};
};

template <typename T>
class CriticalField
{
public:
    ID get(T const& _c)
    {
        auto it = m_criticals.find(_c);
        if (it == m_criticals.end())
        {
            if (m_criticalAll != INVALID_ID)
                return m_criticalAll;
            return INVALID_ID;
        }
        return it->second;
    }

    void update(T const& _c, ID _txId) { m_criticals[_c] = _txId; }

    void foreachField(std::function<void(ID)> _f)
    {
        for (auto const& _fieldAndId : m_criticals)
        {
            _f(_fieldAndId.second);
        }

        if (m_criticalAll != INVALID_ID)
            _f(m_criticalAll);
    }

    void setCriticalAll(ID _id)
    {
        m_criticalAll = _id;
        m_criticals.clear();
    }

private:
    std::map<T, ID> m_criticals;
    ID m_criticalAll = INVALID_ID;
};

}  // namespace executor
}  // namespace bcos
