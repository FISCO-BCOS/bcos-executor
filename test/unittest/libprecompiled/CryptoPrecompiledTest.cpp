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
 * @file CryptoPrecompiledTest.cpp
 * @author: kyonRay
 * @date 2021-07-05
 */

#include "libprecompiled/CryptoPrecompiled.h"
#include "PreCompiledFixture.h"
#include <bcos-framework/testutils/TestPromptFixture.h>
#include <bcos-crypto/signature/sm2/SM2Crypto.h>
#include <bcos-crypto/signature/sm2/SM2KeyPair.h>
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::ledger;
using namespace bcos::crypto;
using namespace bcos::codec;

namespace bcos::test
{
class CryptoPrecompiledFixture : public PrecompiledFixture
{
public:
    CryptoPrecompiledFixture()
    {
        cryptoPrecompiled = std::make_shared<CryptoPrecompiled>(hashImpl);
        setIsWasm(false);
    }

    virtual ~CryptoPrecompiledFixture() {}

    void testHash(CryptoPrecompiled::Ptr _cryptoPrecompiled, BlockContext::Ptr _precompiledContext)
    {
        std::string stringData = "abcd";
        bytesConstRef dataRef(stringData);
        bytes encodedData = codec->encodeWithSig("sm3(bytes)", dataRef.toBytes());
        auto callResult = _cryptoPrecompiled->call(_precompiledContext, bytesConstRef(&encodedData), "", "", gas);
        bytes out = callResult->execResult();
        string32 decodedHash;
        codec->decode(bytesConstRef(&out), decodedHash);
        HashType hash = HashType("82ec580fe6d36ae4f81cae3c73f4a5b3b5a09c943172dc9053c69fd8e18dca1e");
        std::cout << "== testHash-sm3: decodedHash: " <<  codec::fromString32(decodedHash).hex() << std::endl;
        std::cout << "== testHash-sm3: hash:" << hash.hex() << std::endl;
        BOOST_CHECK(hash == codec::fromString32(decodedHash));

        encodedData = codec->encodeWithSig("keccak256Hash(bytes)", dataRef.toBytes());
        callResult =
            _cryptoPrecompiled->call(_precompiledContext, bytesConstRef(&encodedData), "", "", gas);
        out = callResult->execResult();
        codec->decode(bytesConstRef(&out), decodedHash);
        hash = HashType("48bed44d1bcd124a28c27f343a817e5f5243190d3c52bf347daf876de1dbbf77");
        std::cout << "== testHash-keccak256Hash: decodedHash: "
                  << codec::fromString32(decodedHash).hex() << std::endl;
        std::cout << "== testHash-keccak256Hash: hash:" << hash.hex() << std::endl;
        BOOST_CHECK(hash == codec::fromString32(decodedHash));
    }

    CryptoPrecompiled::Ptr cryptoPrecompiled;
    int addressCount = 0x10000;
};
BOOST_FIXTURE_TEST_SUITE(precompiledCryptoTest, CryptoPrecompiledFixture)

BOOST_AUTO_TEST_CASE(testSM3AndKeccak256)
{
    testHash(cryptoPrecompiled, context);
}

BOOST_AUTO_TEST_CASE(testSM2Verify)
{
    // case Verify success
    h256 fixedSec1("bcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd");
    auto sec1 = std::make_shared<KeyImpl>(fixedSec1.asBytes());
    auto keyFactory = std::make_shared<KeyFactoryImpl>();
    auto secCreated = keyFactory->createKey(fixedSec1.asBytes());

    auto keyPair = std::make_shared<SM2KeyPair>(sec1);
    HashType hash = HashType("82ec580fe6d36ae4f81cae3c73f4a5b3b5a09c943172dc9053c69fd8e18dca1e");
    auto signature = sm2Sign(keyPair, hash, true);
    // verify the signature
    bytes encodedData = codec->encodeWithSig("sm2Verify(bytes,bytes)", hash.asBytes(), *signature);
    auto callResult = cryptoPrecompiled->call(context, bytesConstRef(&encodedData), "", "", gas);
    bytes out = callResult->execResult();

    bool verifySucc;
    Address accountAddress;
    codec->decode(bytesConstRef(&out), verifySucc, accountAddress);
    std::cout << "== testSM2Verify-normalCase, verifySucc: " << verifySucc << std::endl;
    std::cout << "== testSM2Verify-normalCase, accountAddress: " << accountAddress.hex()
              << std::endl;
    std::cout << "== realAccountAddress:" << keyPair->address(smHashImpl).hex() << std::endl;
    BOOST_CHECK(verifySucc == true);
    BOOST_CHECK(accountAddress.hex() == keyPair->address(smHashImpl).hex());

    // case mismatch message
    h256 mismatchHash = h256("c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470");
    encodedData =
        codec->encodeWithSig("sm2Verify(bytes,bytes)", mismatchHash.asBytes(), *signature);
    callResult = cryptoPrecompiled->call(context, bytesConstRef(&encodedData), "", "", gas);
    out = callResult->execResult();
    codec->decode(bytesConstRef(&out), verifySucc, accountAddress);
    std::cout << "== testSM2Verify-mismatchHashCase, verifySucc: " << verifySucc << std::endl;
    std::cout << "== testSM2Verify-mismatchHashCase, accountAddress: " << accountAddress.hex()
              << std::endl;
    std::cout << "== realAccountAddress:" << keyPair->address(smHashImpl).hex() << std::endl;
    BOOST_CHECK(verifySucc == false);
    BOOST_CHECK(accountAddress.hex() == Address().hex());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
