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
 * @brief executive of vm
 * @file Executive.cpp
 * @author: xingqiangbai
 * @date: 2021-05-24
 */

#include "Executive.h"
#include "EVMHostInterface.h"
#include "ExecutiveContext.h"
#include "HostContext.h"
#include "VMFactory.h"
#include "VMInstance.h"
#include "bcos-framework/interfaces/protocol/Exceptions.h"
#include "bcos-framework/interfaces/storage/TableInterface.h"
#include "bcos-framework/libcodec/abi/ContractABICodec.h"
#include <limits.h>
#include <boost/timer.hpp>
#include <numeric>

#define EXECUTIVE_LOG(LEVEL) LOG(LEVEL) << "[EXECUTIVE]"

using namespace std;
using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::protocol;
using namespace bcos::codec;

/// Error info for VMInstance status code.
using errinfo_evmcStatusCode = boost::error_info<struct tag_evmcStatusCode, evmc_status_code>;

u256 Executive::gasUsed() const
{
    return m_envInfo->txGasLimit() - m_remainGas;
}

void Executive::accrueSubState(SubState& _parentContext)
{
    if (m_context)
        _parentContext += m_context->sub();
}

void Executive::initialize(Transaction::ConstPtr _transaction)
{
    m_t = _transaction;

    m_baseGasRequired = (m_t->type() == protocol::TransactionType::ContractCreation) ?
                            m_envInfo->evmSchedule().txCreateGas :
                            m_envInfo->evmSchedule().txGas;
    // Calculate the cost of input data.
    // No risk of overflow by using int64 until txDataNonZeroGas is quite small
    // (the value not in billions).
    for (auto i : m_t->input())
        m_baseGasRequired +=
            i ? m_envInfo->evmSchedule().txDataNonZeroGas : m_envInfo->evmSchedule().txDataZeroGas;

    uint64_t txGasLimit = m_envInfo->txGasLimit();
    // The gas limit is dynamic, not fixed.
    // Pre calculate the gas needed for execution
    if (m_baseGasRequired > (bigint)txGasLimit)
    {
        m_excepted = TransactionStatus::OutOfGasLimit;
        m_exceptionReason << LOG_KV("reason",
                                 "The gas required by deploying/accessing this contract is more "
                                 "than tx_gas_limit")
                          << LOG_KV("limit", txGasLimit) << LOG_KV("require", m_baseGasRequired);
        BOOST_THROW_EXCEPTION(
            OutOfGasLimit() << RequirementError((bigint)(m_baseGasRequired), (bigint)txGasLimit));
    }
}

bool Executive::execute()
{
    uint64_t txGasLimit = m_envInfo->txGasLimit();

    if (txGasLimit < (u256)m_baseGasRequired)
    {
        m_excepted = TransactionStatus::OutOfGasLimit;
        m_exceptionReason << LOG_KV("reason",
                                 "The gas required by deploying/accessing this contract is "
                                 "more than tx_gas_limit")
                          << LOG_KV("limit", txGasLimit) << LOG_KV("require", m_baseGasRequired);
        BOOST_THROW_EXCEPTION(
            OutOfGasLimit() << errinfo_comment(
                "Not enough gas, base gas required:" + std::to_string(m_baseGasRequired)));
    }

    if (m_t->type() == TransactionType::ContractCreation)
    {
        return create(fromBytes(m_t->sender()), txGasLimit - (u256)m_baseGasRequired, m_t->input(),
            fromBytes(m_t->sender()));
    }
    else
    {
        return call(fromBytes(m_t->to()), fromBytes(m_t->sender()), m_t->input(),
            txGasLimit - (u256)m_baseGasRequired);
    }
}

bool Executive::call(const std::string& _receiveAddress,
    const std::string& _senderAddress, bytesConstRef _data, u256 const& _gas)
{
    CallParameters params{_senderAddress, _receiveAddress, _receiveAddress, _gas, _data};
    return call(params, _senderAddress);
}

void Executive::updateGas(std::shared_ptr<precompiled::PrecompiledExecResult>)
{
// TODO: calculate gas
#if 0
    auto gasUsed = _callResult->calGasCost();
    if (m_remainGas < gasUsed)
    {
        m_excepted = TransactionStatus::OutOfGas;
        EXECUTIVE_LOG(WARNING) << LOG_DESC("OutOfGas when executing precompiled Contract")
                               << LOG_KV("gasUsed", gasUsed) << LOG_KV("curGas", m_remainGas);
        BOOST_THROW_EXCEPTION(
            PrecompiledError("OutOfGas when executing precompiled Contract, gasUsed: " +
                             boost::lexical_cast<std::string>(gasUsed) +
                             ", leftGas:" + boost::lexical_cast<std::string>(m_remainGas)));
    }
    m_remainGas -= gasUsed;
#endif
}

bool Executive::call(CallParameters const& _p, const std::string& _origin)
{
    // no nonce increase

    m_savepoint = m_s->savepoint();
    m_remainGas = _p.gas;

    if (m_t && m_s->frozen(_origin))
    {
        EXECUTIVE_LOG(DEBUG) << LOG_DESC("execute transaction failed for account frozen")
                             << LOG_KV("account", _origin);
        writeErrInfoToOutput("Frozen account:0x" + string(_origin));
        revert();
        m_excepted = TransactionStatus::AccountFrozen;
        return !m_context;
    }

    if (m_envInfo && m_envInfo->isEthereumPrecompiled(_p.codeAddress))
    {
        auto gas = m_envInfo->costOfPrecompiled(_p.codeAddress, _p.data);
        if (m_remainGas < gas)
        {
            m_excepted = TransactionStatus::OutOfGas;
            // true actually means "all finished - nothing more to be done regarding go().
            return true;
        }
        else
        {
            m_remainGas = (u256)(_p.gas - gas);
        }
        bytes output;
        bool success;
        tie(success, output) =
            m_envInfo->executeOriginPrecompiled(_p.codeAddress, _p.data);
        size_t outputSize = output.size();
        m_output = owning_bytes_ref{std::move(output), 0, outputSize};
        if (!success)
        {
            m_remainGas = 0;
            m_excepted = TransactionStatus::RevertInstruction;
            return true;  // true means no need to run go().
        }
    }
    else if (m_envInfo && m_envInfo->isPrecompiled(_p.codeAddress))
    {
        try
        {
            auto callResult = m_envInfo->call(
                _p.codeAddress, _p.data, _origin, _p.senderAddress, m_remainGas);
            // TODO: calculate gas for the precompiled contract
            // updateGas(callResult);
            size_t outputSize = callResult->m_execResult.size();
            m_output = owning_bytes_ref{std::move(callResult->m_execResult), 0, outputSize};
        }
        catch (protocol::PrecompiledError& e)
        {
            revert();
            m_excepted = TransactionStatus::PrecompiledError;
            writeErrInfoToOutput(e.what());
        }
        catch (Exception& e)
        {
            writeErrInfoToOutput(e.what());
            revert();
            m_excepted = toTransactionStatus(e);
        }
        catch (std::exception& e)
        {
            writeErrInfoToOutput(e.what());
            revert();
            m_excepted = TransactionStatus::Unknown;
        }
    }
    else if (m_s->frozen(_p.codeAddress))
    {
        EXECUTIVE_LOG(DEBUG) << LOG_DESC("execute transaction failed for ContractFrozen")
                             << LOG_KV("contractAddr", _p.codeAddress);
        writeErrInfoToOutput("Frozen contract:" + _p.codeAddress);
        revert();
        m_excepted = TransactionStatus::ContractFrozen;
    }
    else if (m_s->addressHasCode(_p.codeAddress))
    {
        auto c = m_s->code(_p.codeAddress);
        h256 codeHash = m_s->codeHash(_p.codeAddress);
        m_context = make_shared<HostContext>(m_envInfo, _p.receiveAddress, _p.senderAddress,
            _origin, _p.data, c, codeHash, m_depth, false, _p.staticCall);
    }
    else
    {
        writeErrInfoToOutput("Error address:" + _p.codeAddress);
        revert();
        m_excepted = TransactionStatus::CallAddressError;
    }

    // no balance transfer
    return !m_context;
}

bool Executive::create(const std::string_view& _txSender, u256 const& _gas, bytesConstRef _init,
    const std::string_view& _origin)
{  // FIXME: if wasm deploy, then call precompiled of wasm deploy
    // Contract creation by an external account is the same as CREATE opcode
    return createOpcode(_txSender, _gas, _init, _origin);
}

bool Executive::createOpcode(const std::string_view& _sender, u256 const& _gas, bytesConstRef _init,
    const std::string_view& _origin)
{
    u256 nonce = m_s->getNonce(_sender);
    // FIXME:  rlpList(_sender, nonce) ==> string
    m_newAddress = right160(m_hashImpl->hash(string(_sender) + nonce.str())).hexPrefixed();
    return executeCreate(_sender, _gas, _init, _origin);
}

bool Executive::create2Opcode(const std::string_view& _sender, u256 const& _gas,
    bytesConstRef _init, const std::string_view& _origin, u256 const& _salt)
{
    m_newAddress = right160(m_hashImpl->hash(bytes{0xff} + toBytes(_sender) + toBigEndian(_salt) +
                                             m_hashImpl->hash(_init)))
                       .hexPrefixed();
    return executeCreate(_sender, _gas, _init, _origin);
}

bool Executive::executeCreate(const std::string_view& _sender, u256 const& _gas,
    bytesConstRef _init, const std::string_view& _origin, bytesConstRef constructorParams)
{
    // check authority for deploy contract
    auto tableFactory = m_envInfo->getTableFactory();
    if (!tableFactory->checkAuthority(SYS_TABLE, string(_origin)))
    {
        EXECUTIVE_LOG(WARNING) << "Executive deploy contract checkAuthority of " << _origin
                               << " failed!";
        m_remainGas = 0;
        m_excepted = TransactionStatus::PermissionDenied;
        revert();
        m_context = {};
        return !m_context;
    }

    if (m_s->frozen(_origin))
    {
        EXECUTIVE_LOG(DEBUG) << LOG_DESC("deploy contract failed for account frozen")
                             << LOG_KV("account", _origin);
        writeErrInfoToOutput("Frozen account:0x" + string(_origin));
        m_remainGas = 0;
        revert();
        m_excepted = TransactionStatus::AccountFrozen;
        m_context = {};
        return !m_context;
    }

    m_s->incNonce(_sender);

    m_savepoint = m_s->savepoint();

    m_isCreation = true;

    // We can allow for the reverted state (i.e. that with which m_context is constructed) to
    // contain the m_orig.address, since we delete it explicitly if we decide we need to revert.

    m_remainGas = _gas;
    bool accountAlreadyExist =
        (m_s->addressHasCode(m_newAddress) || m_s->getNonce(m_newAddress) > 0);
    if (accountAlreadyExist)
    {
        EXECUTIVE_LOG(TRACE) << "Executive address already used: " << m_newAddress;
        m_remainGas = 0;
        m_excepted = TransactionStatus::ContractAddressAlreadyUsed;
        revert();
        m_context = {};  // cancel the _init execution if there are any scheduled.
        return !m_context;
    }

    // Set nonce before deploying the code. This will also create new
    // account if it does not exist yet.
    m_s->setNonce(m_newAddress, m_s->accountStartNonce());

    grantContractStatusManager(tableFactory, m_newAddress, string(_sender), string(_origin));

    // Schedule _init execution if not empty.
    if (!_init.empty())
    {
        auto code = make_shared<bytes>(_init.data(), _init.data() + _init.size());
        if (hasWasmPreamble(*code))
        {  // FIXME: get the constructor parameters from outside
            // callData = bytesConstRef(m_t->extraData().data(), m_t->extraData().size());
            auto result = m_gasInjector->InjectMeter(*code);
            if (result.status == wasm::GasInjector::Status::Success)
            {
                result.byteCode->swap(*code);
            }
            else
            {
                revert();
                m_context = {};
                m_excepted = TransactionStatus::WASMValidationFailuer;
                EXECUTIVE_LOG(ERROR) << "wasm bytecode invalid or use unsupported opcode";
                return !m_context;
            }
        }
        m_context = make_shared<HostContext>(m_envInfo, m_newAddress, _sender, _origin,
            constructorParams, code, m_hashImpl->hash(_init), m_depth, true, false);
    }
    return !m_context;
}

void Executive::grantContractStatusManager(TableFactoryInterface::Ptr tableFactory,
    const std::string& newAddress, const std::string& sender, const std::string& origin)
{
    EXECUTIVE_LOG(DEBUG) << LOG_DESC("grantContractStatusManager") << LOG_KV("contract", newAddress)
                         << LOG_KV("sender", sender) << LOG_KV("origin", origin);

    std::string tableName = getContractTableName(newAddress);
    auto table = tableFactory->openTable(tableName);

    if (!table)
    {
        EXECUTIVE_LOG(ERROR) << LOG_DESC("grantContractStatusManager get newAddress table error!");
        return;
    }
#if 0
    // grant origin authorization
    auto entry = table->newEntry();
    entry->setField("key", "authority");
    entry->setField("value", origin);
    table->insert("authority", entry);
    EXECUTIVE_LOG(DEBUG) << LOG_DESC("grantContractStatusManager add authoriy")
                         << LOG_KV("origin", origin);
#endif
    if (origin != sender)
    {
        // grant authorization of sender contract
        std::string senderTableName = getContractTableName(sender);
        auto senderTable = tableFactory->openTable(senderTableName);
        if (!senderTable)
        {
            EXECUTIVE_LOG(ERROR) << LOG_DESC("grantContractStatusManager get sender table error!");
            return;
        }
#if 0
// FIXME: authority is not supported for now
        auto entry = senderTable->getRow("authority");
        if (!entry)
        {
            EXECUTIVE_LOG(ERROR) << LOG_DESC(
                "grantContractStatusManager no sender authority is granted");
        }
        else
        {
            for (size_t i = 0; i < entries->size(); i++)
            {
                std::string authority = entries->get(i)->getField("value");
                if (origin != authority)
                {
                    // remove duplicate
                    auto entry = table->newEntry();
                    entry->setField("key", "authority");
                    entry->setField("value", authority);
                    table->insert("authority", entry);
                    EXECUTIVE_LOG(DEBUG) << LOG_DESC("grantContractStatusManager add authoriy")
                                         << LOG_KV("sender", authority);
                }
            }
        }
#endif
    }
    return;
}

bool Executive::go()
{
    if (m_context)
    {
#if ETH_TIMED_EXECUTIONS
        Timer t;
#endif
        try
        {
            auto getEVMCMessage = [=]() -> shared_ptr<evmc_message> {
                // the block number will be larger than 0,
                // can be controlled by the programmers
                assert(m_context->envInfo()->currentNumber() >= 0);
                constexpr int64_t int64max = std::numeric_limits<int64_t>::max();
                if (m_remainGas > int64max || m_context->envInfo()->gasLimit() > int64max)
                {
                    EXECUTIVE_LOG(ERROR) << LOG_DESC("Gas overflow") << LOG_KV("gas", m_remainGas)
                                         << LOG_KV("gasLimit", m_context->envInfo()->gasLimit())
                                         << LOG_KV("max gas/gasLimit", int64max);
                    BOOST_THROW_EXCEPTION(GasOverflow());
                }
                assert(
                    m_context->depth() <= static_cast<size_t>(std::numeric_limits<int32_t>::max()));
                evmc_call_kind kind = m_context->isCreate() ? EVMC_CREATE : EVMC_CALL;
                uint32_t flags = m_context->staticCall() ? EVMC_STATIC : 0;
                // this is ensured by solidity compiler
                assert(flags != EVMC_STATIC || kind == EVMC_CALL);  // STATIC implies a CALL.
                auto leftGas = static_cast<int64_t>(m_remainGas);
                return shared_ptr<evmc_message>(
                    new evmc_message{kind, flags, static_cast<int32_t>(m_context->depth()), leftGas,
                        toEvmC(m_context->myAddress()), toEvmC(m_context->caller()),
                        m_context->data().data(), m_context->data().size(), toEvmC(h256(0)),
                        toEvmC(0x0_cppui256)});
            };
            // Create VM instance.
            auto vmKind = VMKind::evmone;
            if (hasWasmPreamble(*m_context->code()))
            {
                vmKind = VMKind::Hera;
            }
            auto vm = VMFactory::create(vmKind);
            if (m_isCreation)
            {
                m_s->clearStorage(m_context->myAddress());
                auto mode = toRevision(m_context->evmSchedule());
                auto emvcMessage = getEVMCMessage();
                auto ret = vm->exec(m_context, mode, emvcMessage.get(), m_context->code()->data(),
                    m_context->code()->size());
                parseEVMCResult(ret);
                auto outputRef = ret->output();
                if (outputRef.size() > m_context->evmSchedule().maxCodeSize)
                {
                    m_exceptionReason << LOG_KV("reason", "Code is too long")
                                      << LOG_KV("size_limit", m_context->evmSchedule().maxCodeSize)
                                      << LOG_KV("size", outputRef.size());
                    BOOST_THROW_EXCEPTION(OutOfGas());
                }
                else if (outputRef.size() * m_context->evmSchedule().createDataGas <= m_remainGas)
                {
                    m_remainGas -= outputRef.size() * m_context->evmSchedule().createDataGas;
                }
                else
                {
                    if (m_context->evmSchedule().exceptionalFailedCodeDeposit)
                    {
                        m_exceptionReason << LOG_KV("reason", "exceptionalFailedCodeDeposit");
                        BOOST_THROW_EXCEPTION(OutOfGas());
                    }
                    else
                    {
                        outputRef = {};
                    }
                }
                m_s->setCode(m_context->myAddress(), outputRef);
            }
            else
            {
                auto mode = toRevision(m_context->evmSchedule());
                auto emvcMessage = getEVMCMessage();
                auto ret = vm->exec(m_context, mode, emvcMessage.get(), m_context->code()->data(),
                    m_context->code()->size());
                parseEVMCResult(ret);
            }
        }
        catch (RevertInstruction& _e)
        {
            revert();
            writeErrInfoToOutput(_e.what());
            m_excepted = TransactionStatus::RevertInstruction;
        }
        catch (OutOfGas& _e)
        {
            revert();
            m_excepted = TransactionStatus::OutOfGas;
        }
        catch (GasOverflow const& _e)
        {
            revert();
            m_excepted = TransactionStatus::GasOverflow;
        }
#if 0
        catch (VMException const& _e)
        {
            EXECUTIVE_LOG(TRACE) << "Safe VM Exception. " << diagnostic_information(_e);
            m_remainGas = 0;
            m_excepted = toTransactionStatus(_e);
            revert();
        }
#endif
        catch (PermissionDenied const& _e)
        {
            revert();
            m_excepted = TransactionStatus::PermissionDenied;
        }
        catch (NotEnoughCash const& _e)
        {
            revert();
            m_excepted = TransactionStatus::NotEnoughCash;
        }
        catch (PrecompiledError const& _e)
        {
            revert();
            m_excepted = TransactionStatus::PrecompiledError;
        }
        catch (InternalVMError const& _e)
        {
            using errinfo_evmcStatusCode =
                boost::error_info<struct tag_evmcStatusCode, evmc_status_code>;
            EXECUTIVE_LOG(WARNING) << "Internal VM Error ("
                                   << *boost::get_error_info<errinfo_evmcStatusCode>(_e) << ")\n"
                                   << diagnostic_information(_e);
            revert();
            exit(1);
        }
        catch (Exception const& _e)
        {
            // TODO: AUDIT: check that this can never reasonably happen. Consider what to do if it
            // does.
            EXECUTIVE_LOG(ERROR)
                << "Unexpected exception in VM. There may be a bug in this implementation. "
                << diagnostic_information(_e);
            exit(1);
            // Another solution would be to reject this transaction, but that also
            // has drawbacks. Essentially, the amount of ram has to be increased here.
        }
        catch (std::exception const& _e)
        {
            // TODO: AUDIT: check that this can never reasonably happen. Consider what to do if it
            // does.
            EXECUTIVE_LOG(ERROR) << "Unexpected std::exception in VM. Not enough RAM? "
                                 << _e.what();
            exit(1);
            // Another solution would be to reject this transaction, but that also
            // has drawbacks. Essentially, the amount of ram has to be increased here.
        }

#if ETH_TIMED_EXECUTIONS
        cnote << "VM took:" << t.elapsed() << "; gas used: " << (sgas - m_endGas);
#endif
    }
    return true;
}

bool Executive::finalize()
{
    // Accumulate refunds for suicides.
    if (m_context)
        m_context->sub().refunds +=
            m_context->evmSchedule().suicideRefundGas * m_context->sub().suicides.size();

    // Suicides...
    if (m_context)
        for (auto a : m_context->sub().suicides)
            m_s->kill(a);

    // Logs..
    if (m_context)
        m_logs = m_context->sub().logs;

    return (m_excepted == TransactionStatus::None);
}

void Executive::revert()
{
    if (m_context)
        m_context->sub().clear();

    // Set result address to the null one.
    m_newAddress = {};
    m_s->rollback(m_savepoint);
}

void Executive::parseEVMCResult(std::shared_ptr<Result> _result)
{
    // FIXME: if EVMC_REJECTED, then use default vm to run. maybe wasm call evm need this
    auto outputRef = _result->output();
    switch (_result->status())
    {
    case EVMC_SUCCESS:
    {
        m_remainGas = _result->gasLeft();
        if (!m_isCreation)
        {
            m_output = owning_bytes_ref(
                bytes(outputRef.data(), outputRef.data() + outputRef.size()), 0, outputRef.size());
        }
        break;
    }
    case EVMC_REVERT:
    {
        // FIXME: Copy the output for now, but copyless version possible.
        m_remainGas = _result->gasLeft();
        revert();
        m_output = owning_bytes_ref(
            bytes(outputRef.data(), outputRef.data() + outputRef.size()), 0, outputRef.size());
        m_excepted = TransactionStatus::RevertInstruction;
        break;
    }
    case EVMC_OUT_OF_GAS:
    case EVMC_FAILURE:
    {
        revert();
        m_excepted = TransactionStatus::OutOfGas;
        break;
    }

    case EVMC_INVALID_INSTRUCTION:  // NOTE: this could have its own exception
    case EVMC_UNDEFINED_INSTRUCTION:
    {
        m_remainGas = 0;
        m_excepted = TransactionStatus::BadInstruction;
        revert();
        break;
    }

    case EVMC_BAD_JUMP_DESTINATION:
    {
        m_remainGas = 0;
        m_excepted = TransactionStatus::BadJumpDestination;
        revert();
        break;
    }
    case EVMC_STACK_OVERFLOW:
    {
        m_remainGas = 0;
        m_excepted = TransactionStatus::OutOfStack;
        revert();
        break;
    }
    case EVMC_STACK_UNDERFLOW:
    {
        m_remainGas = 0;
        m_excepted = TransactionStatus::StackUnderflow;
        revert();
        break;
    }
    case EVMC_INVALID_MEMORY_ACCESS:
    {
        m_remainGas = 0;
        EXECUTIVE_LOG(WARNING) << LOG_DESC("VM error, BufferOverrun");
        m_excepted = TransactionStatus::StackUnderflow;
        revert();
        break;
    }
    case EVMC_STATIC_MODE_VIOLATION:
    {
        m_remainGas = 0;
        EXECUTIVE_LOG(WARNING) << LOG_DESC("VM error, DisallowedStateChange");
        m_excepted = TransactionStatus::Unknown;
        revert();
        break;
    }
    case EVMC_CONTRACT_VALIDATION_FAILURE:
    {
        EXECUTIVE_LOG(WARNING) << LOG_DESC(
            "WASM validation failed, contract hash algorithm dose not match host.");
        m_excepted = TransactionStatus::WASMValidationFailuer;
        revert();
        break;
    }
    case EVMC_ARGUMENT_OUT_OF_RANGE:
    {
        EXECUTIVE_LOG(WARNING) << LOG_DESC("WASM Argument Out Of Range");
        m_excepted = TransactionStatus::WASMArgumentOutOfRange;
        revert();
        break;
    }
    case EVMC_WASM_UNREACHABLE_INSTRUCTION:
    {
        EXECUTIVE_LOG(WARNING) << LOG_DESC("WASM Unreacheable Instruction");
        m_excepted = TransactionStatus::WASMUnreacheableInstruction;
        revert();
        break;
    }
    case EVMC_INTERNAL_ERROR:
    default:
    {
        if (_result->status() <= EVMC_INTERNAL_ERROR)
        {
            BOOST_THROW_EXCEPTION(InternalVMError{} << errinfo_evmcStatusCode(_result->status()));
        }
        else
        {  // These cases aren't really internal errors, just more specific error codes returned by
           // the VM. Map all of them to OOG.
            BOOST_THROW_EXCEPTION(OutOfGas());
        }
    }
    }
}

void Executive::loggingException()
{
    if (m_excepted != TransactionStatus::None)
    {
        EXECUTIVE_LOG(ERROR) << LOG_BADGE("TxExeError") << LOG_DESC("Transaction execution error")
                             << LOG_KV("TransactionExceptionID", (uint32_t)m_excepted)
                             << LOG_KV("hash", m_t->signatureData().empty() ?
                                                   "call" :
                                                   m_t->hash().hexPrefixed())
                             << m_exceptionReason.str();
    }
}

void Executive::writeErrInfoToOutput(string const& errInfo)
{
    codec::abi::ContractABICodec abi(m_hashImpl);
    auto output = abi.abiIn("Error(string)", errInfo);
    m_output = owning_bytes_ref{std::move(output), 0, output.size()};
}