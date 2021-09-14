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
 * @file TransactionExecutive.cpp
 * @author: xingqiangbai
 * @date: 2021-05-24
 */

#include "TransactionExecutive.h"
#include "../ChecksumAddress.h"
#include "BlockContext.h"
#include "Common.h"
#include "EVMHostInterface.h"
#include "HostContext.h"
#include "VMFactory.h"
#include "VMInstance.h"
#include "bcos-framework/interfaces/protocol/Exceptions.h"
#include "bcos-framework/interfaces/storage/Table.h"
#include "bcos-framework/libcodec/abi/ContractABICodec.h"
#include <limits.h>
#include <boost/lexical_cast.hpp>
#include <numeric>

using namespace std;
using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::protocol;
using namespace bcos::codec;

/// Error info for VMInstance status code.
using errinfo_evmcStatusCode = boost::error_info<struct tag_evmcStatusCode, evmc_status_code>;

namespace
{
evmc_status_code transactionStatusToEvmcStatus(protocol::TransactionStatus ex) noexcept
{
    switch (ex)
    {
    case TransactionStatus::None:
        return EVMC_SUCCESS;

    case TransactionStatus::RevertInstruction:
        return EVMC_REVERT;

    case TransactionStatus::OutOfGas:
        return EVMC_OUT_OF_GAS;

    case TransactionStatus::BadInstruction:
        return EVMC_UNDEFINED_INSTRUCTION;

    case TransactionStatus::OutOfStack:
        return EVMC_STACK_OVERFLOW;

    case TransactionStatus::StackUnderflow:
        return EVMC_STACK_UNDERFLOW;

    case TransactionStatus::BadJumpDestination:
        return EVMC_BAD_JUMP_DESTINATION;

    default:
        return EVMC_FAILURE;
    }
}

}  // namespace

u256 TransactionExecutive::gasLeft() const
{
    return m_remainGas;
}

std::string TransactionExecutive::newAddress() const
{
    if (m_blockContext->isWasm() || m_newAddress.empty())
    {
        return m_newAddress;
    }
    auto hexAddress = toChecksumAddressFromBytes(m_newAddress, m_hashImpl);
    return hexAddress;
}

bool TransactionExecutive::execute()
{
    int64_t txGasLimit = m_blockContext->txGasLimit();

    if (txGasLimit < m_baseGasRequired)
    {
        m_excepted = TransactionStatus::OutOfGasLimit;
        m_exceptionReason =
            "The gas required by deploying/accessing this contract is more than "
            "tx_gas_limit" +
            boost::lexical_cast<std::string>(txGasLimit) +
            " require: " + boost::lexical_cast<std::string>(m_baseGasRequired);
        BOOST_THROW_EXCEPTION(
            OutOfGasLimit() << errinfo_comment(
                "Not enough gas, base gas required:" + std::to_string(m_baseGasRequired)));
    }

    if (m_isCreation)
    {
        return create();
    }
    else
    {
        return call();
    }
}

bool TransactionExecutive::call()
{
    //   m_savepoint = m_s->savepoint();
    m_remainGas = m_callParameters.gas;

    auto precompiledAddress = m_blockContext->isWasm() ? m_callParameters.codeAddress :
                                                         *toHexString(m_callParameters.codeAddress);
    if (m_blockContext && m_blockContext->isEthereumPrecompiled(precompiledAddress))
    {
        auto gas = m_blockContext->costOfPrecompiled(precompiledAddress, m_callParameters.data);
        if (m_remainGas < gas)
        {
            m_excepted = TransactionStatus::OutOfGas;
            // true actually means "all finished - nothing more to be done regarding
            // go().
            return true;
        }
        else
        {
            m_remainGas = m_callParameters.gas - gas;
        }

        auto [success, output] =
            m_blockContext->executeOriginPrecompiled(precompiledAddress, m_callParameters.data);
        if (!success)
        {
            m_remainGas = 0;
            m_excepted = TransactionStatus::RevertInstruction;
            return true;  // true means no need to run go().
        }

        size_t outputSize = output.size();
        m_output = owning_bytes_ref{std::move(output), 0, outputSize};
    }
    else if (m_blockContext && m_blockContext->isPrecompiled(precompiledAddress))
    {
        try
        {
            auto callResult = m_blockContext->call(precompiledAddress, m_callParameters.data,
                m_callParameters.origin, m_callParameters.senderAddress, m_remainGas);
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
            m_excepted = executor::toTransactionStatus(e);
        }
        catch (std::exception& e)
        {
            writeErrInfoToOutput(e.what());
            revert();
            m_excepted = TransactionStatus::Unknown;
        }
    }
    else
    {
        auto table = m_blockContext->storage()->openTable(
            getContractTableName(m_callParameters.receiveAddress, m_blockContext->isWasm(),
                m_blockContext->hashHandler()));
        m_hostContext = make_shared<HostContext>(shared_from_this(), std::move(*table), m_depth);
    }

    // no balance transfer
    return !m_hostContext;
}

// bool TransactionExecutive::executeCreate(const std::string_view& _sender,
//     const std::string_view& _origin, const std::string& _newAddress, int64_t _gasLeft,
//     bytesConstRef _init, bytesConstRef constructorParams)
bool TransactionExecutive::executeCreate()
{
    //   m_s->incNonce(_sender);

    //   m_savepoint = m_s->savepoint();

    // Set nonce before deploying the code. This will also create new
    // account if it does not exist yet.
    //   m_s->setNonce(_newAddress, m_s->accountStartNonce());

    // Schedule _init execution if not empty.
    auto input = m_callParameters.data;
    if (!input.empty())
    {
        auto code = bytes(input.data(), input.data() + input.size());

        if (m_blockContext->isWasm())
        {  // the Wasm deploy use a precompiled which
           // call this function, so inject meter here
            if (!hasWasmPreamble(code))
            {  // if isWASM and the code is not WASM, make it failed
                revert();
                m_hostContext = {};
                m_excepted = TransactionStatus::WASMValidationFailure;
                EXECUTIVE_LOG(ERROR) << "wasm bytecode invalid or use unsupported opcode";
                return !m_hostContext;
            }
            auto result = m_gasInjector->InjectMeter(code);
            if (result.status == wasm::GasInjector::Status::Success)
            {
                result.byteCode->swap(code);
            }
            else
            {
                revert();
                m_hostContext = {};
                m_excepted = TransactionStatus::WASMValidationFailure;
                EXECUTIVE_LOG(ERROR) << "wasm bytecode invalid or use unsupported opcode";
                return !m_hostContext;
            }
        }

        auto newAddress = m_callParameters.receiveAddress;

        // Create the table first
        auto tableName = getContractTableName(
            newAddress, m_blockContext->isWasm(), m_blockContext->hashHandler());
        m_blockContext->storage()->createTable(tableName, STORAGE_VALUE);

        auto table = m_blockContext->storage()->openTable(tableName);

        m_hostContext =
            std::make_shared<HostContext>(shared_from_this(), std::move(*table), m_depth);
    }
    return !m_hostContext;
}

bool TransactionExecutive::go()
{
    if (m_hostContext)
    {
        try
        {
            auto getEVMCMessage = [=]() -> shared_ptr<evmc_message> {
                // the block number will be larger than 0,
                // can be controlled by the programmers
                assert(m_blockContext->currentNumber() > 0);

                constexpr int64_t int64max = std::numeric_limits<int64_t>::max();
                if (m_remainGas > int64max ||
                    m_hostContext->getBlockContext()->gasLimit() > int64max)
                {
                    EXECUTIVE_LOG(ERROR)
                        << LOG_DESC("Gas overflow") << LOG_KV("gas", m_remainGas)
                        << LOG_KV("gasLimit", m_hostContext->getBlockContext()->gasLimit())
                        << LOG_KV("max gas/gasLimit", int64max);
                    BOOST_THROW_EXCEPTION(GasOverflow());
                }
                assert(m_hostContext->depth() <=
                       static_cast<size_t>(std::numeric_limits<int32_t>::max()));
                evmc_call_kind kind = m_hostContext->isCreate() ? EVMC_CREATE : EVMC_CALL;
                uint32_t flags = m_hostContext->staticCall() ? EVMC_STATIC : 0;
                // this is ensured by solidity compiler
                assert(flags != EVMC_STATIC || kind == EVMC_CALL);  // STATIC implies a CALL.
                auto leftGas = static_cast<int64_t>(m_remainGas);
                return shared_ptr<evmc_message>(new evmc_message{kind, flags,
                    static_cast<int32_t>(m_hostContext->depth()), leftGas,
                    toEvmC(m_hostContext->myAddress()), (uint8_t*)m_hostContext->myAddress().data(),
                    (int32_t)m_hostContext->myAddress().size(), toEvmC(m_hostContext->caller()),
                    (uint8_t*)m_hostContext->caller().data(),
                    (int32_t)m_hostContext->caller().size(), m_hostContext->data().data(),
                    m_hostContext->data().size(), toEvmC(h256(0)), toEvmC(0x0_cppui256)});
            };

            auto code = m_hostContext->code();

            auto vmKind = VMKind::evmone;
            if (hasWasmPreamble(code))
            {
                vmKind = VMKind::Hera;
            }
            auto vm = VMFactory::create(vmKind);

            if (m_isCreation)
            {
                auto mode = toRevision(m_hostContext->evmSchedule());
                auto evmcMessage = getEVMCMessage();

                auto ret =
                    vm->exec(m_hostContext, mode, evmcMessage.get(), code.data(), code.size());
                parseEVMCResult(ret);

                auto outputRef = ret->output();
                if (outputRef.size() > m_hostContext->evmSchedule().maxCodeSize)
                {
                    m_exceptionReason =
                        "Code is too log: " + boost::lexical_cast<std::string>(outputRef.size()) +
                        " limit: " +
                        boost::lexical_cast<std::string>(m_hostContext->evmSchedule().maxCodeSize);
                    BOOST_THROW_EXCEPTION(OutOfGas());
                }

                if (outputRef.size() * m_hostContext->evmSchedule().createDataGas >
                    (uint64_t)m_remainGas)
                {
                    if (m_hostContext->evmSchedule().exceptionalFailedCodeDeposit)
                    {
                        m_exceptionReason = "exceptionalFailedCodeDeposit";
                        BOOST_THROW_EXCEPTION(OutOfGas());
                    }
                    else
                    {
                        outputRef = {};
                    }
                }

                m_remainGas -= outputRef.size() * m_hostContext->evmSchedule().createDataGas;

                m_hostContext->setCode(outputRef.toBytes());
            }
            else
            {
                auto mode = toRevision(m_hostContext->evmSchedule());
                auto evmcMessage = getEVMCMessage();
                auto ret =
                    vm->exec(m_hostContext, mode, evmcMessage.get(), code.data(), code.size());
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
            m_excepted = executor::toTransactionStatus(_e);
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
            // TODO: AUDIT: check that this can never reasonably happen. Consider what
            // to do if it does.
            EXECUTIVE_LOG(ERROR) << "Unexpected exception in VM. There may be a bug "
                                    "in this implementation. "
                                 << diagnostic_information(_e);
            exit(1);
            // Another solution would be to reject this transaction, but that also
            // has drawbacks. Essentially, the amount of ram has to be increased here.
        }
        catch (std::exception const& _e)
        {
            // TODO: AUDIT: check that this can never reasonably happen. Consider what
            // to do if it does.
            EXECUTIVE_LOG(ERROR) << "Unexpected std::exception in VM. Not enough RAM? "
                                 << _e.what();
            exit(1);
            // Another solution would be to reject this transaction, but that also
            // has drawbacks. Essentially, the amount of ram has to be increased here.
        }
    }
    return true;
}

evmc_result TransactionExecutive::waitReturnValue(
    Error::Ptr e, protocol::ExecutionResult::Ptr result)
{
    // EXTERNAL_CALL, set m_waitResult with promise and call the returnCallback,
    // when future return continue to run
    promise<evmc_result> prom;
    m_waitResult = [&prom, callCreate = m_callCreate](bytes&& output, int32_t status,
                       int64_t gasLeft, std::string_view newAddress) {
        evmc_result result;
        result.status_code = transactionStatusToEvmcStatus((protocol::TransactionStatus)status);
        result.gas_left = gasLeft;
        if (callCreate && result.status_code == EVMC_SUCCESS)
        {
            result.release = nullptr;
            result.create_address = toEvmC(newAddress);
            result.output_data = nullptr;
            result.output_size = 0;
        }
        else
        {
            // Pass the output to the EVM without a copy. The EVM will delete it
            // when finished with it.

            // First assign reference. References are not invalidated when vector
            // of bytes is moved. See `.takeBytes()` below.
            result.output_data = output.data();
            result.output_size = output.size();

            // Place a new vector of bytes containing output in result's reserved
            // memory.
            auto* data = evmc_get_optional_storage(&result);
            static_assert(sizeof(bytes) <= sizeof(*data), "Vector is too big");
            new (data) bytes(move(output));
            // Set the destructor to delete the vector.
            result.release = [](evmc_result const* _result) {
                // check _result is not null
                if (_result == NULL)
                {
                    return;
                }
                auto* data = evmc_get_const_optional_storage(_result);
                auto& output = reinterpret_cast<bytes const&>(*data);
                // Explicitly call vector's destructor to release its data.
                // This is normal pattern when placement new operator is used.
                output.~bytes();
            };
            prom.set_value(result);
        }
    };
    m_returnCallback(std::move(e), move(result));
    return prom.get_future().get();
}

bool TransactionExecutive::continueExecution(
    bytes&& output, int32_t status, int64_t gasLeft, std::string_view newAddress)
{
    if (m_waitResult)
    {
        m_waitResult(std::forward<bytes>(output), status, gasLeft, newAddress);
        return true;
    }
    return false;
}

bool TransactionExecutive::finalize()
{
    // Accumulate refunds for suicides.
    if (m_hostContext)
        m_hostContext->sub().refunds +=
            m_hostContext->evmSchedule().suicideRefundGas * m_hostContext->sub().suicides.size();

    // Logs..
    if (m_hostContext)
        m_logs = m_hostContext->sub().logs;
    m_finished = true;
    return (m_excepted == TransactionStatus::None);
}

void TransactionExecutive::revert()
{
    if (m_hostContext)
        m_hostContext->sub().clear();

    // Set result address to the null one.
    m_newAddress = {};
    // m_s.rollback(m_savepoint); // TODO: new revert plan
}

void TransactionExecutive::parseEVMCResult(std::shared_ptr<Result> _result)
{
    // FIXME: if EVMC_REJECTED, then use default vm to run. maybe wasm call evm
    // need this
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
        m_excepted = TransactionStatus::WASMValidationFailure;
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
        EXECUTIVE_LOG(WARNING) << LOG_DESC("WASM Unreachable Instruction");
        m_excepted = TransactionStatus::WASMUnreachableInstruction;
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
        {  // These cases aren't really internal errors, just more specific
           // error codes returned by the VM. Map all of them to OOG.
            BOOST_THROW_EXCEPTION(OutOfGas());
        }
    }
    }
}

void TransactionExecutive::loggingException()
{
    if (m_excepted != TransactionStatus::None)
    {
        EXECUTIVE_LOG(ERROR) << LOG_BADGE("TxExeError") << LOG_DESC("Transaction execution error")
                             << LOG_KV("TransactionExceptionID", (uint32_t)m_excepted)
                             << LOG_KV("hash", m_callParameters.staticCall ? "call" : "execute")
                             << m_exceptionReason;
    }
}

void TransactionExecutive::writeErrInfoToOutput(string const& errInfo)
{
    codec::abi::ContractABICodec abi(m_hashImpl);
    auto output = abi.abiIn("Error(string)", errInfo);
    m_output = owning_bytes_ref{std::move(output), 0, output.size()};
}

std::string TransactionExecutive::getContractTableName(
    const std::string_view& _address, bool _isWasm, crypto::Hash::Ptr _hashImpl)
{
    if (_isWasm)
    {
        return std::string(_address);
    }
    auto address = toChecksumAddressFromBytes(_address, _hashImpl);
    return std::string("c_").append(address);
}