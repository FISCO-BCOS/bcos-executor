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

#include "TxDAG.h"
#include "../libvm/Executive.h"
#include "Common.h"
#include <tbb/parallel_for.h>
#include <map>

using namespace std;
using namespace bcos;
using namespace bcos::executor;

#define DAG_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("DAG")

// Generate DAG according with given transactions
void TxDAG::init(
    ExecutiveContext::Ptr _ctx, const protocol::Block::Ptr& _block)
{
    m_block = _block;
    auto txsSize = m_block->transactionsSize();
    DAG_LOG(TRACE) << LOG_DESC("Begin init transaction DAG") << LOG_KV("blockHeight", m_block->blockHeader()->number())
                   << LOG_KV("transactionNum", txsSize);
    m_dag.init(txsSize);

    // get criticals
    std::vector<std::shared_ptr<std::vector<std::string>>> txsCriticals;
    txsCriticals.resize(txsSize);
    tbb::parallel_for(
        tbb::blocked_range<uint64_t>(0, txsSize), [&](const tbb::blocked_range<uint64_t>& range) {
            for (uint64_t i = range.begin(); i < range.end(); i++)
            {
                txsCriticals[i] = _ctx->getTxCriticals(m_block->transaction(i));
            }
        });

    CriticalField<string> latestCriticals;

    for (ID id = 0; id < txsSize; ++id)
    {
        auto criticals = txsCriticals[id];
        if (criticals)
        {
            // DAG transaction: Conflict with certain critical fields
            // Get critical field

            // Add edge between critical transaction
            for (string const& c : *criticals)
            {
                ID pId = latestCriticals.get(c);
                if (pId != INVALID_ID)
                {
                    m_dag.addEdge(pId, id);  // add DAG edge
                }
            }

            for (string const& c : *criticals)
            {
                latestCriticals.update(c, id);
            }
        }
        else
        {
            // Normal transaction: Conflict with all transaction
            latestCriticals.foreachField([&](ID _fieldId) {
                ID pId = _fieldId;
                // Add edge from all critical transaction
                m_dag.addEdge(pId, id);
            });

            // set all critical to my id
            latestCriticals.setCriticalAll(id);
        }
    }

    // Generate DAG
    m_dag.generate();

    m_totalParaTxs = txsSize;

    DAG_LOG(TRACE) << LOG_DESC("End init transaction DAG") << LOG_KV("blockHeight", m_block->blockHeader()->number());
}

// Set transaction execution function
void TxDAG::setTxExecuteFunc(ExecuteTxFunc const& _f)
{
    f_executeTx = _f;
}

int TxDAG::executeUnit(Executive::Ptr _executive)
{
    int exeCnt = 0;
    ID id = m_dag.waitPop();
    while (id != INVALID_ID)
    {
        do
        {
            exeCnt += 1;
            f_executeTx(m_block->transaction(id), id, _executive);
            id = m_dag.consume(id);
        } while (id != INVALID_ID);
        id = m_dag.waitPop();
    }
    if (exeCnt > 0)
    {
        Guard l(x_exeCnt);
        m_exeCnt += exeCnt;
    }
    return exeCnt;
}
