/**
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
 * @file CryptoPrecompiled.cpp
 * @author: kyonRay
 * @date 2021-05-30
 */

#include "CryptoPrecompiled.h"
#include "Utilities.h"
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/sm2/SM2Crypto.h>
#include <bcos-crypto/signature/ed25519/Ed25519Crypto.h>
#include <bcos-framework/libcodec/abi/ContractABICodec.h>
#include <bcos-framework/interfaces/crypto/Signature.h>

using namespace bcos;
using namespace bcos::codec;
using namespace bcos::crypto;
using namespace bcos::executor;
using namespace bcos::precompiled;

// precompiled interfaces related to hash calculation
const char* const CRYPTO_METHOD_SM3_STR = "sm3(bytes)";
// Note: the interface here can't be keccak256k1 for naming conflict
const char* const CRYPTO_METHOD_KECCAK256_STR = "keccak256Hash(bytes)";
// precompiled interfaces related to verify
// sm2 verify: (message, sign)
const char* const CRYPTO_METHOD_SM2_VERIFY_STR = "sm2Verify(bytes,bytes)";
// precompiled interfaces related to VRF verify
// the params are (vrfInput, vrfPublicKey, vrfProof)
const char* const CRYPTO_METHOD_CURVE25519_VRF_VERIFY_STR =
    "curve25519VRFVerify(string,string,string)";

CryptoPrecompiled::CryptoPrecompiled()
{
    name2Selector[CRYPTO_METHOD_SM3_STR] = getFuncSelector(CRYPTO_METHOD_SM3_STR);
    name2Selector[CRYPTO_METHOD_KECCAK256_STR] = getFuncSelector(CRYPTO_METHOD_KECCAK256_STR);
    name2Selector[CRYPTO_METHOD_SM2_VERIFY_STR] = getFuncSelector(CRYPTO_METHOD_SM2_VERIFY_STR);
    name2Selector[CRYPTO_METHOD_CURVE25519_VRF_VERIFY_STR] =
        getFuncSelector(CRYPTO_METHOD_CURVE25519_VRF_VERIFY_STR);
}

PrecompiledExecResult::Ptr CryptoPrecompiled::call(
    std::shared_ptr<executor::ExecutiveContext>,
    bytesConstRef _param, const std::string&, const std::string&,
    u256& _remainGas)
{
    auto funcSelector = getParamFunc(_param);
    auto paramData = getParamData(_param);
    codec::abi::ContractABICodec abi(nullptr);
    auto callResult = m_precompiledExecResultFactory->createPrecompiledResult();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();
    gasPricer->setMemUsed(_param.size());
    if (funcSelector == name2Selector[CRYPTO_METHOD_SM3_STR])
    {
        bytes inputData;
        abi.abiOut(paramData, inputData);

        auto sm3Hash = crypto::sm3Hash(ref(inputData));
        PRECOMPILED_LOG(TRACE) << LOG_DESC("CryptoPrecompiled: sm3")
                               << LOG_KV("input", toHexString(inputData))
                               << LOG_KV("result", toHexString(sm3Hash));
        callResult->setExecResult(abi.abiIn("", codec::toString32(sm3Hash)));
    }
    else if (funcSelector == name2Selector[CRYPTO_METHOD_KECCAK256_STR])
    {
        bytes inputData;
        abi.abiOut(paramData, inputData);
        auto keccak256Hash = crypto::keccak256Hash(ref(inputData));
        PRECOMPILED_LOG(TRACE) << LOG_DESC("CryptoPrecompiled: keccak256")
                               << LOG_KV("input", toHexString(inputData))
                               << LOG_KV("result", toHexString(keccak256Hash));
        callResult->setExecResult(abi.abiIn("", codec::toString32(keccak256Hash)));
    }
    else if (funcSelector == name2Selector[CRYPTO_METHOD_SM2_VERIFY_STR])
    {
        sm2Verify(paramData, callResult);
    }
    else if (funcSelector == name2Selector[CRYPTO_METHOD_CURVE25519_VRF_VERIFY_STR])
    {
        curve25519VRFVerify(paramData, callResult);
    }
    else
    {
        // no defined function
        PRECOMPILED_LOG(ERROR) << LOG_DESC("CryptoPrecompiled: undefined method")
                               << LOG_KV("funcSelector", std::to_string(funcSelector));
        callResult->setExecResult(abi.abiIn("", u256(int(CODE_UNKNOW_FUNCTION_CALL))));
    }
    gasPricer->updateMemUsed(callResult->m_execResult.size());
    _remainGas -= gasPricer->calTotalGas();
    return callResult;
}

void CryptoPrecompiled::sm2Verify(bytesConstRef _paramData, PrecompiledExecResult::Ptr _callResult)
{
    codec::abi::ContractABICodec abi(nullptr);
    try
    {
        bytes message;
        bytes sm2Sign;
        abi.abiOut(_paramData, message, sm2Sign);

        auto msgHash = HashType(asString(message));
        Address account;
        bool verifySuccess = true;
        auto publicKey = crypto::sm2Recover(msgHash, ref(sm2Sign));
        if (!publicKey)
        {
            PRECOMPILED_LOG(DEBUG)
                    << LOG_DESC("CryptoPrecompiled: sm2Verify failed for recover public key failed");
            _callResult->setExecResult(abi.abiIn("", false, account));
            return;
        }

        account = right160(crypto::sm3Hash(ref(publicKey->data())));
        PRECOMPILED_LOG(TRACE) << LOG_DESC("CryptoPrecompiled: sm2Verify")
                               << LOG_KV("verifySuccess", verifySuccess)
                               << LOG_KV("publicKey", toHexString(publicKey->data()))
                               << LOG_KV("account", account);
        _callResult->setExecResult(abi.abiIn("", verifySuccess, account));
    }
    catch (std::exception const& e)
    {
        PRECOMPILED_LOG(WARNING) << LOG_DESC("CryptoPrecompiled: sm2Verify exception")
                                 << LOG_KV("e", boost::diagnostic_information(e));
        Address emptyAccount;
        _callResult->setExecResult(abi.abiIn("", false, emptyAccount));
    }
}

void CryptoPrecompiled::curve25519VRFVerify(
    bytesConstRef, PrecompiledExecResult::Ptr _callResult)
{
    PRECOMPILED_LOG(TRACE) << LOG_DESC("CryptoPrecompiled: curve25519VRFVerify");
    codec::abi::ContractABICodec abi(nullptr);
    try
    {
        // TODO: it depends bcos-crypto
//        std::string vrfPublicKey;
//        std::string vrfInput;
//        std::string vrfProof;
//        abi.abiOut(_paramData, vrfInput, vrfPublicKey, vrfProof);
//        u256 randomValue = 0;
//        // check the public key and verify the proof
//        if (0 == curve25519_vrf_is_valid_pubkey(vrfPublicKey.c_str()) &&
//            0 == curve25519_vrf_verify(vrfPublicKey.c_str(), vrfInput.c_str(), vrfProof.c_str()))
//        {
//            // get the random hash
//            auto hexVRFHash = curve25519_vrf_proof_to_hash(vrfProof.c_str());
//            randomValue = (u256)(h256(hexVRFHash));
//            _callResult->setExecResult(abi.abiIn("", true, randomValue));
//            PRECOMPILED_LOG(DEBUG) << LOG_DESC("CryptoPrecompiled: curve25519VRFVerify succ")
//                                   << LOG_KV("vrfHash", hexVRFHash);
//            return;
//        }
//        _callResult->setExecResult(abi.abiIn("", false, randomValue));
//        PRECOMPILED_LOG(DEBUG) << LOG_DESC("CryptoPrecompiled: curve25519VRFVerify failed");
    }
    catch (std::exception const& e)
    {
        PRECOMPILED_LOG(WARNING) << LOG_DESC("CryptoPrecompiled: curve25519VRFVerify exception")
                                 << LOG_KV("e", boost::diagnostic_information(e));
        _callResult->setExecResult(abi.abiIn("", false, u256(0)));
    }
}
