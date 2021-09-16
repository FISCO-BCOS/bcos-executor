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
#include "../executor/Common.h"
#include "BlockContext.h"
#include "Common.h"
#include "EVMHostInterface.h"
#include "HostContext.h"
#include "VMFactory.h"
#include "VMInstance.h"
#include "bcos-framework/interfaces/protocol/Exceptions.h"
#include "bcos-framework/interfaces/storage/Table.h"
#include "bcos-framework/libcodec/abi/ContractABICodec.h"
#include "libutilities/Common.h"
#include <limits.h>
#include <boost/algorithm/hex.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/lexical_cast.hpp>
#include <functional>
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
// evmc_status_code transactionStatusToEvmcStatus(protocol::TransactionStatus ex) noexcept
// {
//     switch (ex)
//     {
//     case TransactionStatus::None:
//         return EVMC_SUCCESS;

//     case TransactionStatus::RevertInstruction:
//         return EVMC_REVERT;

//     case TransactionStatus::OutOfGas:
//         return EVMC_OUT_OF_GAS;

//     case TransactionStatus::BadInstruction:
//         return EVMC_UNDEFINED_INSTRUCTION;

//     case TransactionStatus::OutOfStack:
//         return EVMC_STACK_OVERFLOW;

//     case TransactionStatus::StackUnderflow:
//         return EVMC_STACK_UNDERFLOW;

//     case TransactionStatus::BadJumpDestination:
//         return EVMC_BAD_JUMP_DESTINATION;

//     default:
//         return EVMC_FAILURE;
//     }
// }

}  // namespace

// int64_t TransactionExecutive::gasLeft() const
// {
//     return m_remainGas;
// }

// std::string TransactionExecutive::newAddress() const
// {
//     if (m_blockContext->isWasm() || m_newAddress.empty())
//     {
//         return m_newAddress;
//     }
//     auto hexAddress = toChecksumAddressFromBytes(m_newAddress, m_hashImpl);
//     return hexAddress;
// }

void TransactionExecutive::start(CallParameters::Ptr callParameters)
{
    m_pushMessage = std::make_unique<Coroutine::push_type>(
        Coroutine::push_type([this](Coroutine::pull_type& source) {
            EXECUTOR_LOG(TRACE) << "start coroutine run";

            m_pullMessage = std::make_unique<Coroutine::pull_type>(std::move(source));
            auto callParameters = m_pullMessage->get();

            auto response = execute(callParameters);

            m_callback(shared_from_this(), std::move(response));

            EXECUTOR_LOG(TRACE) << "execution finished";
        }));

    pushMessage(std::move(callParameters));
}

CallParameters::Ptr TransactionExecutive::externalRequest(CallParameters::Ptr input)
{
    m_callback(shared_from_this(), std::move(input));
    (*m_pullMessage)();  // move to the main coroutine

    auto callParameters = m_pullMessage->get();  // wait for main coroutine's response

    return callParameters;
}

CallParameters::Ptr TransactionExecutive::execute(CallParameters::ConstPtr callParameters)
{
    int64_t txGasLimit = m_blockContext->txGasLimit();

    if (txGasLimit < m_baseGasRequired)
    {
        auto callResults = std::make_shared<CallParameters>();
        callResults->status = (int32_t)TransactionStatus::OutOfGasLimit;
        callResults->message =
            "The gas required by deploying/accessing this contract is more than "
            "tx_gas_limit" +
            boost::lexical_cast<std::string>(txGasLimit) +
            " require: " + boost::lexical_cast<std::string>(m_baseGasRequired);

        return callResults;
    }

    std::shared_ptr<HostContext> hostContext;
    CallParameters::Ptr callResults;
    if (callParameters->create)
    {
        std::tie(hostContext, callResults) = create(std::move(callParameters));
    }
    else
    {
        std::tie(hostContext, callResults) = call(std::move(callParameters));
    }

    if (hostContext)
    {
        callResults = go(hostContext);

        hostContext->sub().refunds +=
            hostContext->evmSchedule().suicideRefundGas * hostContext->sub().suicides.size();

        // Logs..
        m_logs = hostContext->sub().logs;
    }

    return callResults;
}

std::tuple<std::shared_ptr<HostContext>, CallParameters::Ptr> TransactionExecutive::call(
    CallParameters::ConstPtr callParameters)
{
    //   m_savepoint = m_s->savepoint();
    auto remainGas = callParameters->gas;

    auto precompiledAddress = callParameters->codeAddress;
    if (m_blockContext && m_blockContext->isEthereumPrecompiled(precompiledAddress))
    {
        auto callResults = std::make_shared<CallParameters>();
        auto gas = m_blockContext->costOfPrecompiled(precompiledAddress, ref(callParameters->data));
        if (remainGas < gas)
        {
            callResults->status = (int32_t)TransactionStatus::OutOfGas;
            return {nullptr, callResults};
        }
        else
        {
            remainGas = remainGas - gas;
        }

        auto [success, output] =
            m_blockContext->executeOriginPrecompiled(precompiledAddress, ref(callParameters->data));
        if (!success)
        {
            callResults->status = (int32_t)TransactionStatus::RevertInstruction;
            return {nullptr, callResults};
        }

        // size_t outputSize = output.size();
        // m_output = owning_bytes_ref{std::move(output), 0, outputSize};

        callResults->data.swap(output);

        return {nullptr, callResults};
    }
    // else if (m_blockContext && m_blockContext->isPrecompiled(precompiledAddress))
    // {
    //     try
    //     {
    //         auto callResult = m_blockContext->call(precompiledAddress, m_callParameters.data,
    //             m_callParameters.origin, m_callParameters.senderAddress, m_remainGas);
    //         size_t outputSize = callResult->m_execResult.size();
    //         m_output = owning_bytes_ref{std::move(callResult->m_execResult), 0, outputSize};
    //     }
    //     catch (protocol::PrecompiledError& e)
    //     {
    //         revert();
    //         m_excepted = TransactionStatus::PrecompiledError;
    //         writeErrInfoToOutput(e.what());
    //     }
    //     catch (Exception& e)
    //     {
    //         writeErrInfoToOutput(e.what());
    //         revert();
    //         m_excepted = executor::toTransactionStatus(e);
    //     }
    //     catch (std::exception& e)
    //     {
    //         writeErrInfoToOutput(e.what());
    //         revert();
    //         m_excepted = TransactionStatus::Unknown;
    //     }
    // }
    else
    {
        auto table = m_blockContext->storage()->openTable(
            getContractTableName(callParameters->receiveAddress, m_blockContext->isWasm()));
        auto hostContext = make_shared<HostContext>(
            shared_from_this(), std::move(callParameters), std::move(*table));

        return {hostContext, nullptr};
    }
}

std::tuple<std::shared_ptr<HostContext>, CallParameters::Ptr> TransactionExecutive::create(
    CallParameters::ConstPtr callParameters)
{
    //   m_s->incNonce(_sender);

    //   m_savepoint = m_s->savepoint();

    // Set nonce before deploying the code. This will also create new
    // account if it does not exist yet.
    //   m_s->setNonce(_newAddress, m_s->accountStartNonce());

    // Schedule _init execution if not empty.
    auto input = callParameters->data;
    auto code = bytes(input.data(), input.data() + input.size());

    if (m_blockContext->isWasm())
    {
        // the Wasm deploy use a precompiled which
        // call this function, so inject meter here
        if (!hasWasmPreamble(code))
        {  // if isWASM and the code is not WASM, make it failed
            auto callResults = std::make_shared<CallParameters>();

            // revert();
            callResults->status = (int32_t)TransactionStatus::WASMValidationFailure;
            callResults->message = "wasm bytecode invalid or use unsupported opcode";
            EXECUTIVE_LOG(ERROR) << callResults->message;
            return {nullptr, callResults};
        }

        auto result = m_gasInjector->InjectMeter(code);
        if (result.status == wasm::GasInjector::Status::Success)
        {
            result.byteCode->swap(code);
        }
        else
        {
            // revert();
            auto callResults = std::make_shared<CallParameters>();

            callResults->status = (int32_t)TransactionStatus::WASMValidationFailure;
            callResults->message = "wasm bytecode invalid or use unsupported opcode";
            EXECUTIVE_LOG(ERROR) << callResults->message;
            return {nullptr, callResults};
        }
    }

    auto newAddress = callParameters->codeAddress;

    // Create the table first
    auto tableName = getContractTableName(newAddress, m_blockContext->isWasm());
    m_blockContext->storage()->createTable(tableName, STORAGE_VALUE);

    auto table = m_blockContext->storage()->openTable(tableName);

    auto hostContext = std::make_shared<HostContext>(
        shared_from_this(), std::move(callParameters), std::move(*table));

    return {hostContext, nullptr};
}

CallParameters::Ptr TransactionExecutive::go(std::shared_ptr<HostContext> hostContext)
{
    auto callResults = std::make_shared<CallParameters>();
    callResults->gas = hostContext->gas();
    try
    {
        auto getEVMCMessage = [this, hostContext]() -> shared_ptr<evmc_message> {
            // the block number will be larger than 0,
            // can be controlled by the programmers
            assert(m_blockContext->currentNumber() > 0);

            assert(
                hostContext->depth() <= static_cast<size_t>(std::numeric_limits<int32_t>::max()));
            evmc_call_kind kind = hostContext->isCreate() ? EVMC_CREATE : EVMC_CALL;
            uint32_t flags = hostContext->staticCall() ? EVMC_STATIC : 0;
            // this is ensured by solidity compiler
            assert(flags != EVMC_STATIC || kind == EVMC_CALL);  // STATIC implies a CALL.
            auto leftGas = static_cast<int64_t>(hostContext->gas());

            auto evmcMessage = std::make_shared<evmc_message>();
            evmcMessage->kind = kind;
            evmcMessage->flags = flags;
            evmcMessage->depth = hostContext->depth();
            evmcMessage->gas = leftGas;
            evmcMessage->input_data = hostContext->data().data();
            evmcMessage->input_size = hostContext->data().size();
            evmcMessage->value = toEvmC(h256(0));
            evmcMessage->create2_salt = toEvmC(0x0_cppui256);

            if (m_blockContext->isWasm())
            {
                evmcMessage->destination_ptr = (uint8_t*)hostContext->myAddress().data();
                evmcMessage->destination_len = hostContext->codeAddress().size();

                evmcMessage->sender_ptr = (uint8_t*)hostContext->caller().data();
                evmcMessage->sender_len = hostContext->caller().size();
            }
            else
            {
                auto myAddressBytes =
                    boost::algorithm::unhex(std::string(hostContext->myAddress()));
                auto callerBytes = boost::algorithm::unhex(std::string(hostContext->caller()));

                evmcMessage->destination = toEvmC(myAddressBytes);
                evmcMessage->destination_ptr = evmcMessage->destination.bytes;
                evmcMessage->destination_len = sizeof(evmcMessage->destination.bytes);
                evmcMessage->sender = toEvmC(callerBytes);
                evmcMessage->sender_ptr = evmcMessage->sender.bytes;
                evmcMessage->sender_len = sizeof(evmcMessage->sender.bytes);
            }

            return evmcMessage;
        };

        if (hostContext->isCreate())
        {
            auto mode = toRevision(hostContext->evmSchedule());
            auto evmcMessage = getEVMCMessage();

            auto code = hostContext->data();
            auto vmKind = VMKind::evmone;
            if (hasWasmPreamble(code))
            {
                vmKind = VMKind::Hera;
            }
            auto vm = VMFactory::create(vmKind);

            auto ret = vm->exec(hostContext, mode, evmcMessage.get(), code.data(), code.size());
            callResults = parseEVMCResult(hostContext->isCreate(), ret);

            auto outputRef = ret->output();
            if (outputRef.size() > hostContext->evmSchedule().maxCodeSize)
            {
                callResults->status = (int32_t)TransactionStatus::OutOfGas;
                callResults->message =
                    "Code is too log: " + boost::lexical_cast<std::string>(outputRef.size()) +
                    " limit: " +
                    boost::lexical_cast<std::string>(hostContext->evmSchedule().maxCodeSize);

                return callResults;
            }

            if ((int64_t)(outputRef.size() * hostContext->evmSchedule().createDataGas) >
                hostContext->gas())
            {
                if (hostContext->evmSchedule().exceptionalFailedCodeDeposit)
                {
                    callResults->status = (int32_t)TransactionStatus::OutOfGas;
                    callResults->message = "exceptionalFailedCodeDeposit";

                    return callResults;
                }
            }

            callResults->gas -= outputRef.size() * hostContext->evmSchedule().createDataGas;
            callResults->newEVMContractAddress = hostContext->codeAddress();

            hostContext->setCode(outputRef.toBytes());
        }
        else
        {
            auto code = hostContext->code();
            auto vmKind = VMKind::evmone;
            if (hasWasmPreamble(code))
            {
                vmKind = VMKind::Hera;
            }
            auto vm = VMFactory::create(vmKind);

            auto mode = toRevision(hostContext->evmSchedule());
            auto evmcMessage = getEVMCMessage();
            auto ret = vm->exec(hostContext, mode, evmcMessage.get(), code.data(), code.size());
            callResults = parseEVMCResult(hostContext->isCreate(), ret);
        }
    }
    catch (RevertInstruction& _e)
    {
        // revert();
        // writeErrInfoToOutput(_e.what());
        callResults->status = (int32_t)TransactionStatus::RevertInstruction;
    }
    catch (OutOfGas& _e)
    {
        // revert();
        callResults->status = (int32_t)TransactionStatus::OutOfGas;
    }
    catch (GasOverflow const& _e)
    {
        // revert();
        callResults->status = (int32_t)TransactionStatus::GasOverflow;
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
        // revert();
        callResults->status = (int32_t)TransactionStatus::PermissionDenied;
    }
    catch (NotEnoughCash const& _e)
    {
        // revert();
        callResults->status = (int32_t)TransactionStatus::NotEnoughCash;
    }
    catch (PrecompiledError const& _e)
    {
        // revert();
        callResults->status = (int32_t)TransactionStatus::PrecompiledError;
    }
    catch (InternalVMError const& _e)
    {
        using errinfo_evmcStatusCode =
            boost::error_info<struct tag_evmcStatusCode, evmc_status_code>;
        EXECUTIVE_LOG(WARNING) << "Internal VM Error ("
                               << *boost::get_error_info<errinfo_evmcStatusCode>(_e) << ")\n"
                               << diagnostic_information(_e);
        // revert();
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
    catch (std::exception& _e)
    {
        // TODO: AUDIT: check that this can never reasonably happen. Consider what
        // to do if it does.
        EXECUTIVE_LOG(ERROR) << "Unexpected std::exception in VM. Not enough RAM? " << _e.what();
        exit(1);
        // Another solution would be to reject this transaction, but that also
        // has drawbacks. Essentially, the amount of ram has to be increased here.
    }
    return callResults;
}

// evmc_result TransactionExecutive::waitReturnValue(
//     Error::Ptr e, protocol::ExecutionResult::Ptr result)
// {
//     // EXTERNAL_CALL, set m_waitResult with promise and call the returnCallback,
//     // when future return continue to run
//     promise<evmc_result> prom;
//     m_waitResult = [this, &prom](bytes&& output, int32_t status, int64_t gasLeft,
//                        std::string_view newAddress) {
//         evmc_result result;
//         result.status_code = transactionStatusToEvmcStatus((protocol::TransactionStatus)status);
//         result.gas_left = gasLeft;
//         if (isCreate() && result.status_code == EVMC_SUCCESS)
//         {
//             result.release = nullptr;
//             result.create_address = toEvmC(newAddress);
//             result.output_data = nullptr;
//             result.output_size = 0;
//         }
//         else
//         {
//             // Pass the output to the EVM without a copy. The EVM will delete it
//             // when finished with it.

//             // First assign reference. References are not invalidated when vector
//             // of bytes is moved. See `.takeBytes()` below.
//             result.output_data = output.data();
//             result.output_size = output.size();

//             // Place a new vector of bytes containing output in result's reserved
//             // memory.
//             auto* data = evmc_get_optional_storage(&result);
//             static_assert(sizeof(bytes) <= sizeof(*data), "Vector is too big");
//             new (data) bytes(move(output));
//             // Set the destructor to delete the vector.
//             result.release = [](evmc_result const* _result) {
//                 // check _result is not null
//                 if (_result == NULL)
//                 {
//                     return;
//                 }
//                 auto* data = evmc_get_const_optional_storage(_result);
//                 auto& output = reinterpret_cast<bytes const&>(*data);
//                 // Explicitly call vector's destructor to release its data.
//                 // This is normal pattern when placement new operator is used.
//                 output.~bytes();
//             };
//             prom.set_value(result);
//         }
//     };
//     m_returnCallback(std::move(e), move(result));
//     return prom.get_future().get();
// }

// bool TransactionExecutive::continueExecution(
//     bytes&& output, int32_t status, int64_t gasLeft, std::string_view newAddress)
// {
//     if (m_waitResult)
//     {
//         m_waitResult(std::forward<bytes>(output), status, gasLeft, newAddress);
//         return true;
//     }
//     return false;
// }

// void TransactionExecutive::revert()
// {
//     if (m_hostContext)
//         m_hostContext->sub().clear();

//     // Set result address to the null one.
//     m_newAddress = {};
//     // m_s.rollback(m_savepoint); // TODO: new revert plan
// }

CallParameters::Ptr TransactionExecutive::parseEVMCResult(
    bool isCreate, std::shared_ptr<Result> _result)
{
    auto callResults = std::make_shared<CallParameters>();

    // FIXME: if EVMC_REJECTED, then use default vm to run. maybe wasm call evm
    // need this
    auto outputRef = _result->output();
    switch (_result->status())
    {
    case EVMC_SUCCESS:
    {
        callResults->gas = _result->gasLeft();
        if (!isCreate)
        {
            callResults->data.assign(outputRef.begin(), outputRef.end());
            // m_output = owning_bytes_ref(
            //     bytes(outputRef.data(), outputRef.data() + outputRef.size()), 0,
            //     outputRef.size());
        }
        break;
    }
    case EVMC_REVERT:
    {
        // FIXME: Copy the output for now, but copyless version possible.
        callResults->gas = _result->gasLeft();
        // revert();
        callResults->data.assign(outputRef.begin(), outputRef.end());
        // m_output = owning_bytes_ref(
        //     bytes(outputRef.data(), outputRef.data() + outputRef.size()), 0, outputRef.size());
        callResults->status = (int32_t)TransactionStatus::RevertInstruction;
        // m_excepted = TransactionStatus::RevertInstruction;
        break;
    }
    case EVMC_OUT_OF_GAS:
    case EVMC_FAILURE:
    {
        // revert();
        callResults->status = (int32_t)TransactionStatus::OutOfGas;
        break;
    }

    case EVMC_INVALID_INSTRUCTION:  // NOTE: this could have its own exception
    case EVMC_UNDEFINED_INSTRUCTION:
    {
        // m_remainGas = 0; //TODO: why set remainGas to 0?
        callResults->status = (int32_t)TransactionStatus::BadInstruction;
        // revert();
        break;
    }
    case EVMC_BAD_JUMP_DESTINATION:
    {
        // m_remainGas = 0;
        callResults->status = (int32_t)TransactionStatus::BadJumpDestination;
        // revert();
        break;
    }
    case EVMC_STACK_OVERFLOW:
    {
        // m_remainGas = 0;
        callResults->status = (int32_t)TransactionStatus::OutOfStack;
        // revert();
        break;
    }
    case EVMC_STACK_UNDERFLOW:
    {
        // m_remainGas = 0;
        callResults->status = (int32_t)TransactionStatus::StackUnderflow;
        // revert();
        break;
    }
    case EVMC_INVALID_MEMORY_ACCESS:
    {
        // m_remainGas = 0;
        EXECUTIVE_LOG(WARNING) << LOG_DESC("VM error, BufferOverrun");
        callResults->status = (int32_t)TransactionStatus::StackUnderflow;
        // revert();
        break;
    }
    case EVMC_STATIC_MODE_VIOLATION:
    {
        // m_remainGas = 0;
        EXECUTIVE_LOG(WARNING) << LOG_DESC("VM error, DisallowedStateChange");
        callResults->status = (int32_t)TransactionStatus::Unknown;
        // revert();
        break;
    }
    case EVMC_CONTRACT_VALIDATION_FAILURE:
    {
        EXECUTIVE_LOG(WARNING) << LOG_DESC(
            "WASM validation failed, contract hash algorithm dose not match host.");
        callResults->status = (int32_t)TransactionStatus::WASMValidationFailure;
        // revert();
        break;
    }
    case EVMC_ARGUMENT_OUT_OF_RANGE:
    {
        EXECUTIVE_LOG(WARNING) << LOG_DESC("WASM Argument Out Of Range");
        callResults->status = (int32_t)TransactionStatus::WASMArgumentOutOfRange;
        // revert();
        break;
    }
    case EVMC_WASM_UNREACHABLE_INSTRUCTION:
    {
        EXECUTIVE_LOG(WARNING) << LOG_DESC("WASM Unreachable Instruction");
        callResults->status = (int32_t)TransactionStatus::WASMUnreachableInstruction;
        // revert();
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

    return callResults;
}

// void TransactionExecutive::writeErrInfoToOutput(string const& errInfo)
// {
//     codec::abi::ContractABICodec abi(m_hashImpl);
//     auto output = abi.abiIn("Error(string)", errInfo);
//     m_output = owning_bytes_ref{std::move(output), 0, output.size()};
// }

std::string TransactionExecutive::getContractTableName(
    const std::string_view& _address, bool _isWasm)
{
    if (_isWasm)
    {
        return std::string(_address);
    }

    std::string address(_address);

    return std::string("c_").append(address);
}