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
#include "../Common.h"
#include "../vm/EVMHostInterface.h"
#include "../vm/HostContext.h"
#include "../vm/Precompiled.h"
#include "../vm/VMFactory.h"
#include "../vm/VMInstance.h"
#include "BlockContext.h"
#include "bcos-executor/TransactionExecutor.h"
#include "bcos-framework/interfaces/protocol/Exceptions.h"
#include "bcos-framework/interfaces/storage/Table.h"
#include "bcos-framework/libcodec/abi/ContractABICodec.h"
#include "libutilities/Common.h"
#include <limits.h>
#include <boost/algorithm/hex.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/throw_exception.hpp>
#include <functional>
#include <numeric>
#include <thread>

using namespace std;
using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::protocol;
using namespace bcos::codec;
using namespace bcos::precompiled;

/// Error info for VMInstance status code.
using errinfo_evmcStatusCode = boost::error_info<struct tag_evmcStatusCode, evmc_status_code>;

void TransactionExecutive::start(CallParameters::UniquePtr input)
{
    auto blockContext = m_blockContext.lock();
    if (!blockContext)
    {
        BOOST_THROW_EXCEPTION(BCOS_ERROR(-1, "blockContext is null"));
    }

    m_storageWrapper = std::make_unique<CoroutineStorageWrapper<CoroutineMessage>>(
        blockContext->storage(), *m_pushMessage, *m_pullMessage);

    m_pushMessage = std::make_unique<Coroutine::push_type>([this](Coroutine::pull_type& source) {
        m_pullMessage = std::make_unique<Coroutine::pull_type>(std::move(source));
        auto callParameters = m_pullMessage->get();

        execute(std::move(std::get<CallParameters::UniquePtr>(callParameters)));

        // EXECUTOR_LOG(TRACE) << "Switching coroutine";
    });

    pushMessage(std::move(input));
}

CallParameters::UniquePtr TransactionExecutive::externalCall(CallParameters::UniquePtr input)
{
    std::optional<CallParameters::UniquePtr> value;

    m_externalCallFunction(m_blockContext.lock(), shared_from_this(), std::move(input),
        [this, threadID = std::this_thread::get_id(), value = &value](
            Error::UniquePtr error, CallParameters::UniquePtr response) {
            EXECUTOR_LOG(TRACE) << "Invoke external call callback";
            (void)error;

            // TODO: ensure the logic common
            // if (std::this_thread::get_id() == threadID)
            if (false)
            {
                *value = std::make_optional(std::move(response));
            }
            else
            {
                (*m_pushMessage)(CallMessage(std::move(response)));
            }
        });

    if (!value)
    {
        (*m_pullMessage)();  // move to the main coroutine
        value = std::make_optional(std::get<CallMessage>(m_pullMessage->get()));
    }

    auto output = std::move(*value);

    // After coroutine switch, set the recoder
    m_storageWrapper->setRecoder(m_recoder);

    return output;
}

CallParameters::UniquePtr TransactionExecutive::execute(CallParameters::UniquePtr callParameters)
{
    // Control by scheduler
    // int64_t txGasLimit = m_blockContext.lock()->txGasLimit();

    // if (txGasLimit < m_baseGasRequired)
    // {
    //     auto callResults = std::make_unique<CallParameters>(CallParameters::REVERT);
    //     callResults->status = (int32_t)TransactionStatus::OutOfGasLimit;
    //     callResults->message =
    //         "The gas required by deploying/accessing this contract is more than "
    //         "tx_gas_limit" +
    //         boost::lexical_cast<std::string>(txGasLimit) +
    //         " require: " + boost::lexical_cast<std::string>(m_baseGasRequired);

    //     return callResults;
    // }
    assert(!m_finished);

    m_storageWrapper->setRecoder(m_recoder);

    std::unique_ptr<HostContext> hostContext;
    CallParameters::UniquePtr callResults;
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
        callResults = go(*hostContext);

        // TODO: check if needed
        hostContext->sub().refunds +=
            hostContext->evmSchedule().suicideRefundGas * hostContext->sub().suicides.size();
    }

    // Current executive is finished
    m_finished = true;
    m_externalCallFunction(m_blockContext.lock(), shared_from_this(), std::move(callResults), {});

    return nullptr;
}

std::tuple<std::unique_ptr<HostContext>, CallParameters::UniquePtr> TransactionExecutive::call(
    CallParameters::UniquePtr callParameters)
{
    auto blockContext = m_blockContext.lock();
    if (!blockContext)
    {
        BOOST_THROW_EXCEPTION(BCOS_ERROR(-1, "blockContext is null"));
    }

    auto precompiledAddress = callParameters->codeAddress;
    if (isPrecompiled(precompiledAddress))
    {
        auto callResults = std::make_unique<CallParameters>(CallParameters::FINISHED);
        try
        {
            auto precompiledResult = callPrecompiled(precompiledAddress, ref(callParameters->data),
                callParameters->origin, callParameters->senderAddress);
            auto gas = precompiledResult->m_gas;
            if (callParameters->gas < gas)
            {
                callResults->type = CallParameters::REVERT;
                callResults->status = (int32_t)TransactionStatus::OutOfGas;
                return {nullptr, std::move(callResults)};
            }
            callParameters->gas -= gas;
            callResults->status = (int32_t)TransactionStatus::None;
            callResults->data.swap(precompiledResult->m_execResult);
        }
        catch (protocol::PrecompiledError& e)
        {
            writeErrInfoToOutput(e.what(), callResults->data);
            revert();
            callResults->status = (int32_t)TransactionStatus::PrecompiledError;
        }
        catch (Exception& e)
        {
            writeErrInfoToOutput(e.what(), callResults->data);
            revert();
            callResults->status = (int32_t)executor::toTransactionStatus(e);
        }
        catch (std::exception& e)
        {
            writeErrInfoToOutput(e.what(), callResults->data);
            revert();
            callResults->status = (int32_t)TransactionStatus::Unknown;
        }
        return {nullptr, std::move(callResults)};
    }
    else
    {
        auto tableName = getContractTableName(callParameters->codeAddress);
        auto hostContext = make_unique<HostContext>(
            std::move(callParameters), shared_from_this(), std::move(tableName));

        return {std::move(hostContext), nullptr};
    }
}

std::tuple<std::unique_ptr<HostContext>, CallParameters::UniquePtr> TransactionExecutive::create(
    CallParameters::UniquePtr callParameters)
{
    auto blockContext = m_blockContext.lock();
    if (!blockContext)
    {
        BOOST_THROW_EXCEPTION(BCOS_ERROR(-1, "blockContext is null"));
    }

    // Schedule _init execution if not empty.
    auto& code = callParameters->data;

    if (blockContext->isWasm())
    {
        // the Wasm deploy use a precompiled which
        // call this function, so inject meter here
        if (!hasWasmPreamble(code))
        {  // if isWASM and the code is not WASM, make it failed
            revert();

            auto callResults = std::move(callParameters);
            callResults->type = CallParameters::REVERT;
            callResults->status = (int32_t)TransactionStatus::WASMValidationFailure;
            callResults->message = "wasm bytecode invalid or use unsupported opcode";
            EXECUTIVE_LOG(ERROR) << callResults->message;
            return {nullptr, std::move(callResults)};
        }

        auto result = m_gasInjector->InjectMeter(code);
        if (result.status == wasm::GasInjector::Status::Success)
        {
            result.byteCode->swap(code);
        }
        else
        {
            revert();

            auto callResults = std::move(callParameters);
            callResults->type = CallParameters::REVERT;
            callResults->status = (int32_t)TransactionStatus::WASMValidationFailure;
            callResults->message = "wasm bytecode invalid or use unsupported opcode";
            EXECUTIVE_LOG(ERROR) << callResults->message;
            return {nullptr, std::move(callResults)};
        }
    }

    auto& newAddress = callParameters->codeAddress;

    // Create the table first
    auto tableName = getContractTableName(newAddress);
    m_storageWrapper->createTable(tableName, STORAGE_VALUE);
    // Create auth table
    creatAuthTable(tableName, callParameters->origin, callParameters->senderAddress);

    auto hostContext = std::make_unique<HostContext>(
        std::move(callParameters), shared_from_this(), std::move(tableName));

    return {std::move(hostContext), nullptr};
}

CallParameters::UniquePtr TransactionExecutive::go(HostContext& hostContext)
{
    try
    {
        auto getEVMCMessage = [](const BlockContext& blockContext,
                                  const HostContext& hostContext) -> evmc_message {
            // the block number will be larger than 0,
            // can be controlled by the programmers
            assert(blockContext.number() > 0);

            evmc_call_kind kind = hostContext.isCreate() ? EVMC_CREATE : EVMC_CALL;
            uint32_t flags = hostContext.staticCall() ? EVMC_STATIC : 0;
            // this is ensured by solidity compiler
            assert(flags != EVMC_STATIC || kind == EVMC_CALL);  // STATIC implies a CALL.
            auto leftGas = hostContext.gas();

            evmc_message evmcMessage;
            evmcMessage.kind = kind;
            evmcMessage.flags = flags;
            evmcMessage.depth = 0;  // depth own by scheduler
            evmcMessage.gas = leftGas;
            evmcMessage.input_data = hostContext.data().data();
            evmcMessage.input_size = hostContext.data().size();
            evmcMessage.value = toEvmC(h256(0));
            evmcMessage.create2_salt = toEvmC(0x0_cppui256);

            if (blockContext.isWasm())
            {
                evmcMessage.destination_ptr = (uint8_t*)hostContext.myAddress().data();
                evmcMessage.destination_len = hostContext.codeAddress().size();

                evmcMessage.sender_ptr = (uint8_t*)hostContext.caller().data();
                evmcMessage.sender_len = hostContext.caller().size();
            }
            else
            {
                auto myAddressBytes = boost::algorithm::unhex(std::string(hostContext.myAddress()));
                auto callerBytes = boost::algorithm::unhex(std::string(hostContext.caller()));

                evmcMessage.destination = toEvmC(myAddressBytes);
                evmcMessage.sender = toEvmC(callerBytes);
            }

            return evmcMessage;
        };

        auto blockContext = m_blockContext.lock();
        if (!blockContext)
        {
            BOOST_THROW_EXCEPTION(BCOS_ERROR(-1, "blockContext is null!"));
        }

        if (hostContext.isCreate())
        {
            auto mode = toRevision(hostContext.evmSchedule());
            auto evmcMessage = getEVMCMessage(*blockContext, hostContext);

            auto code = hostContext.data();
            auto vmKind = VMKind::evmone;
            if (hasWasmPreamble(code))
            {
                vmKind = VMKind::Hera;
            }
            auto vm = VMFactory::create(vmKind);

            auto ret = vm.exec(hostContext, mode, &evmcMessage, code.data(), code.size());

            auto callResults = hostContext.takeCallParameters();
            callResults = parseEVMCResult(std::move(callResults), ret);

            auto outputRef = ret.output();
            if (outputRef.size() > hostContext.evmSchedule().maxCodeSize)
            {
                callResults->type = CallParameters::REVERT;
                callResults->status = (int32_t)TransactionStatus::OutOfGas;
                callResults->message =
                    "Code is too large: " + boost::lexical_cast<std::string>(outputRef.size()) +
                    " limit: " +
                    boost::lexical_cast<std::string>(hostContext.evmSchedule().maxCodeSize);

                return callResults;
            }

            if ((int64_t)(outputRef.size() * hostContext.evmSchedule().createDataGas) >
                callResults->gas)
            {
                if (hostContext.evmSchedule().exceptionalFailedCodeDeposit)
                {
                    callResults->type = CallParameters::REVERT;
                    callResults->status = (int32_t)TransactionStatus::OutOfGas;
                    callResults->message = "exceptionalFailedCodeDeposit";

                    return callResults;
                }
            }

            hostContext.setCode(outputRef.toBytes());

            callResults->gas -= outputRef.size() * hostContext.evmSchedule().createDataGas;
            callResults->newEVMContractAddress = callResults->codeAddress;

            // Clear the create flag
            callResults->create = false;

            // Clear the data
            callResults->data.clear();

            return callResults;
        }
        else
        {
            auto code = hostContext.code();
            if (code.empty())
            {
                BOOST_THROW_EXCEPTION(BCOS_ERROR(-1, "Code not found! " + m_contractAddress));
            }

            auto vmKind = VMKind::evmone;
            if (hasWasmPreamble(code))
            {
                vmKind = VMKind::Hera;
            }
            auto vm = VMFactory::create(vmKind);

            auto mode = toRevision(hostContext.evmSchedule());
            auto evmcMessage = getEVMCMessage(*blockContext, hostContext);
            auto ret = vm.exec(hostContext, mode, &evmcMessage, code.data(), code.size());

            auto callResults = hostContext.takeCallParameters();
            callResults = parseEVMCResult(std::move(callResults), ret);

            return callResults;
        }
    }
    catch (RevertInstruction& _e)
    {
        // writeErrInfoToOutput(_e.what());
        auto callResults = hostContext.takeCallParameters();
        callResults->type = CallParameters::REVERT;
        callResults->status = (int32_t)TransactionStatus::RevertInstruction;
        revert();
    }
    catch (OutOfGas& _e)
    {
        auto callResults = hostContext.takeCallParameters();
        callResults->type = CallParameters::REVERT;
        callResults->status = (int32_t)TransactionStatus::OutOfGas;
        revert();
    }
    catch (GasOverflow const& _e)
    {
        auto callResults = hostContext.takeCallParameters();
        callResults->type = CallParameters::REVERT;
        callResults->status = (int32_t)TransactionStatus::GasOverflow;
        revert();
    }
    catch (PermissionDenied const& _e)
    {
        auto callResults = hostContext.takeCallParameters();
        callResults->type = CallParameters::REVERT;
        callResults->status = (int32_t)TransactionStatus::PermissionDenied;
        revert();
    }
    catch (NotEnoughCash const& _e)
    {
        auto callResults = hostContext.takeCallParameters();
        callResults->type = CallParameters::REVERT;
        callResults->status = (int32_t)TransactionStatus::NotEnoughCash;
        revert();
    }
    catch (PrecompiledError const& _e)
    {
        auto callResults = hostContext.takeCallParameters();
        callResults->type = CallParameters::REVERT;
        callResults->status = (int32_t)TransactionStatus::PrecompiledError;
        revert();
    }
    catch (InternalVMError const& _e)
    {
        auto callResults = hostContext.takeCallParameters();
        callResults->type = CallParameters::REVERT;
        using errinfo_evmcStatusCode =
            boost::error_info<struct tag_evmcStatusCode, evmc_status_code>;
        EXECUTIVE_LOG(WARNING) << "Internal VM Error ("
                               << *boost::get_error_info<errinfo_evmcStatusCode>(_e) << ")\n"
                               << diagnostic_information(_e);
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

    return nullptr;
}

std::shared_ptr<precompiled::PrecompiledExecResult> TransactionExecutive::callPrecompiled(
    const std::string& address, bytesConstRef param, const std::string& origin,
    const std::string& sender)
{
    try
    {
        auto blockContext = this->blockContext().lock();
        auto p = getPrecompiled(address);

        if (p)
        {
            auto execResult = p->call(shared_from_this(), param, origin, sender);
            return execResult;
        }
        else
        {
            EXECUTIVE_LOG(DEBUG) << LOG_DESC("[call]Can't find address")
                                 << LOG_KV("address", address);
            return nullptr;
        }
    }
    catch (protocol::PrecompiledError& e)
    {
        EXECUTIVE_LOG(ERROR) << "PrecompiledError" << LOG_KV("address", address)
                             << LOG_KV("message:", e.what());
        BOOST_THROW_EXCEPTION(e);
    }
    catch (std::exception& e)
    {
        EXECUTIVE_LOG(ERROR) << LOG_DESC("[call]Precompiled call error")
                             << LOG_KV("EINFO", boost::diagnostic_information(e));

        throw PrecompiledError();
    }
}

string TransactionExecutive::registerPrecompiled(std::shared_ptr<precompiled::Precompiled> p)
{
    auto count = ++m_addressCount;
    std::stringstream stream;
    stream << std::setfill('0') << std::setw(40) << std::hex << count;
    auto address = stream.str();
    m_dynamicPrecompiled.insert(std::make_pair(address, p));
    return address;
}

bool TransactionExecutive::isPrecompiled(const std::string& address) const
{
    return (m_constantPrecompiled.count(address) > 0 || m_dynamicPrecompiled.count(address) > 0);
}

std::shared_ptr<Precompiled> TransactionExecutive::getPrecompiled(const std::string& address) const
{
    auto constantPrecompiled = m_constantPrecompiled.find(address);
    auto dynamicPrecompiled = m_dynamicPrecompiled.find(address);

    if (constantPrecompiled != m_constantPrecompiled.end())
    {
        return constantPrecompiled->second;
    }
    if (dynamicPrecompiled != m_dynamicPrecompiled.end())
    {
        return dynamicPrecompiled->second;
    }
    return std::shared_ptr<precompiled::Precompiled>();
}

bool TransactionExecutive::isEthereumPrecompiled(const string& _a) const
{
    return m_evmPrecompiled->find(_a) != m_evmPrecompiled->end();
}

std::pair<bool, bcos::bytes> TransactionExecutive::executeOriginPrecompiled(
    const string& _a, bytesConstRef _in) const
{
    return m_evmPrecompiled->at(_a)->execute(_in);
}

int64_t TransactionExecutive::costOfPrecompiled(const string& _a, bytesConstRef _in) const
{
    return m_evmPrecompiled->at(_a)->cost(_in).convert_to<int64_t>();
}

void TransactionExecutive::setEVMPrecompiled(
    std::shared_ptr<const std::map<std::string, PrecompiledContract::Ptr>> precompiledContract)
{
    m_evmPrecompiled = std::move(precompiledContract);
}
void TransactionExecutive::setConstantPrecompiled(
    const string& address, std::shared_ptr<precompiled::Precompiled> precompiled)
{
    m_constantPrecompiled.insert(std::make_pair(address, precompiled));
}
void TransactionExecutive::setConstantPrecompiled(
    std::map<std::string, std::shared_ptr<precompiled::Precompiled>> _constantPrecompiled)
{
    m_constantPrecompiled = std::move(_constantPrecompiled);
}

void TransactionExecutive::revert()
{
    auto blockContext = m_blockContext.lock();
    if (!blockContext)
    {
        BOOST_THROW_EXCEPTION(BCOS_ERROR(-1, "blockContext is null!"));
    }

    blockContext->storage()->rollback(m_recoder);
}

CallParameters::UniquePtr TransactionExecutive::parseEVMCResult(
    CallParameters::UniquePtr callResults, const Result& _result)
{
    callResults->type = CallParameters::REVERT;
    // FIXME: if EVMC_REJECTED, then use default vm to run. maybe wasm call evm
    // need this
    auto outputRef = _result.output();
    switch (_result.status())
    {
    case EVMC_SUCCESS:
    {
        callResults->type = CallParameters::FINISHED;
        callResults->status = _result.status();
        callResults->gas = _result.gasLeft();
        if (!callResults->create)
        {
            callResults->data.assign(outputRef.begin(), outputRef.end());  // TODO: avoid the data
                                                                           // copy
        }
        break;
    }
    case EVMC_REVERT:
    {
        // FIXME: Copy the output for now, but copyless version possible.
        callResults->gas = _result.gasLeft();
        revert();
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
        revert();
        callResults->status = (int32_t)TransactionStatus::OutOfGas;
        break;
    }

    case EVMC_INVALID_INSTRUCTION:  // NOTE: this could have its own exception
    case EVMC_UNDEFINED_INSTRUCTION:
    {
        // m_remainGas = 0; //TODO: why set remainGas to 0?
        callResults->status = (int32_t)TransactionStatus::BadInstruction;
        revert();
        break;
    }
    case EVMC_BAD_JUMP_DESTINATION:
    {
        // m_remainGas = 0;
        callResults->status = (int32_t)TransactionStatus::BadJumpDestination;
        revert();
        break;
    }
    case EVMC_STACK_OVERFLOW:
    {
        // m_remainGas = 0;
        callResults->status = (int32_t)TransactionStatus::OutOfStack;
        revert();
        break;
    }
    case EVMC_STACK_UNDERFLOW:
    {
        // m_remainGas = 0;
        callResults->status = (int32_t)TransactionStatus::StackUnderflow;
        revert();
        break;
    }
    case EVMC_INVALID_MEMORY_ACCESS:
    {
        // m_remainGas = 0;
        EXECUTIVE_LOG(WARNING) << LOG_DESC("VM error, BufferOverrun");
        callResults->status = (int32_t)TransactionStatus::StackUnderflow;
        revert();
        break;
    }
    case EVMC_STATIC_MODE_VIOLATION:
    {
        // m_remainGas = 0;
        EXECUTIVE_LOG(WARNING) << LOG_DESC("VM error, DisallowedStateChange");
        callResults->status = (int32_t)TransactionStatus::Unknown;
        revert();
        break;
    }
    case EVMC_CONTRACT_VALIDATION_FAILURE:
    {
        EXECUTIVE_LOG(WARNING) << LOG_DESC(
            "WASM validation failed, contract hash algorithm dose not match host.");
        callResults->status = (int32_t)TransactionStatus::WASMValidationFailure;
        revert();
        break;
    }
    case EVMC_ARGUMENT_OUT_OF_RANGE:
    {
        EXECUTIVE_LOG(WARNING) << LOG_DESC("WASM Argument Out Of Range");
        callResults->status = (int32_t)TransactionStatus::WASMArgumentOutOfRange;
        revert();
        break;
    }
    case EVMC_WASM_UNREACHABLE_INSTRUCTION:
    {
        EXECUTIVE_LOG(WARNING) << LOG_DESC("WASM Unreachable Instruction");
        callResults->status = (int32_t)TransactionStatus::WASMUnreachableInstruction;
        revert();
        break;
    }
    case EVMC_INTERNAL_ERROR:
    default:
    {
        revert();
        if (_result.status() <= EVMC_INTERNAL_ERROR)
        {
            BOOST_THROW_EXCEPTION(InternalVMError{} << errinfo_evmcStatusCode(_result.status()));
        }
        else
        {  // These cases aren't really internal errors, just more specific
           // error codes returned by the VM. Map all of them to OOG.m_externalCallCallback
            BOOST_THROW_EXCEPTION(OutOfGas());
        }
    }
    }

    return callResults;
}

void TransactionExecutive::creatAuthTable(
    std::string_view _tableName, std::string_view _origin, std::string_view _sender)
{
    // Create the access table
    // TODO: use global variant,
    //  /sys/ not create
    if (_tableName.substr(0, 4) == "/sys/")
        return;
    auto authTableName = std::string(_tableName).append("_accessAuth");
    // if contract external create contract, then inheritance agent
    std::string_view agent;
    if (_sender != _origin)
    {
        auto senderAuthTable = getContractTableName(_sender).append("_accessAuth");
        auto entry = m_storageWrapper->getRow(std::move(senderAuthTable), "agent");
        agent = entry->getField(STORAGE_VALUE);
    }
    auto table = m_storageWrapper->createTable(authTableName, STORAGE_VALUE);
    auto agentEntry = table->newEntry();
    agentEntry.setField(STORAGE_VALUE, std::string(agent));
    m_storageWrapper->setRow(authTableName, "agent", std::move(agentEntry));
    m_storageWrapper->setRow(authTableName, "interface_auth", table->newEntry());
}
