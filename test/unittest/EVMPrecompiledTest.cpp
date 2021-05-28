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
 * @file EVMPrecompiledTest.cpp
 * @author: xingqiangbai
 * @date 2021-06-02
 */

#include "bcos-framework/libutilities/DataConvertUtility.h"
#include "libvm/ECRecover.h"
#include "libvm/RIPEMD160.h"
#include "libvm/SHA256.h"
#include <boost/test/unit_test.hpp>
#include <iostream>

using namespace std;
using namespace bcos;
using namespace bcos::crypto;

namespace bcos
{
namespace test
{
struct EVMPrecompiledTestFixture
{
    EVMPrecompiledTestFixture() {}
};

BOOST_FIXTURE_TEST_SUITE(EVMPrecompiledTest, EVMPrecompiledTestFixture)

/// test ecRecover
BOOST_AUTO_TEST_CASE(testECRecover)
{
    std::pair<bool, bytes> keyPair;
    std::pair<bool, bytes> KeyPairR;
    bytes rlpBytes = *fromHexString(
        "f8ef9f65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d03f85174876e7ff"
        "8609184e729fff82020394d6f1a71052366dbae2f7ab2d5d5845e77965cf0d80b86448f85bce000000"
        "000000000000000000000000000000000000000000000000000000001bf5bd8a9e7ba8b936ea704292"
        "ff4aaa5797bf671fdc8526dcd159f23c1f5a05f44e9fa862834dc7cb4541558f2b4961dc39eaaf0af7"
        "f7395028658d0e01b86a371ca00b2b3fabd8598fefdda4efdb54f626367fc68e1735a8047f0f1c4f84"
        "0255ca1ea0512500bc29f4cfe18ee1c88683006d73e56c934100b8abf4d2334560e1d2f75e");

    bytes rlpBytesRight = *fromHexString(
        "38d18acb67d25c8bb9942764b62f18e17054f66a817bd4295423adf9ed98873e"
        "000000000000000000000000000000000000000000000000000000000000001b"
        "38d18acb67d25c8bb9942764b62f18e17054f66a817bd4295423adf9ed98873e"
        "789d1dd423d25f0772d2748d60f7e4b81bb14d086eba8e8e8efb6dcff8a4ae02");

    h256 ret("000000000000000000000000ceaccac640adf55b2028469bd36ba501f28b699d");
    bytesConstRef _in(ref(rlpBytes));
    keyPair = ecRecover(_in);
    BOOST_CHECK(keyPair.first == true);
    BOOST_CHECK(keyPair.second != ret.asBytes());
    KeyPairR = ecRecover(ref(rlpBytesRight));
    cout << toHexStringWithPrefix(KeyPairR.second) << endl;
    cout << toHexStringWithPrefix(ret.asBytes()) << endl;
    BOOST_CHECK(KeyPairR.second == ret.asBytes());
}

// test sha2
BOOST_AUTO_TEST_CASE(testSha256)
{
    const std::string plainText = "123456ABC+";
    h256 cipherText("0x2218be4abd327ca929399fc73314f3d0cdd03cfc98927fabe7cd40f2059efd01");
    bytes bs;
    for (size_t i = 0; i < plainText.length(); i++)
    {
        bs.push_back((byte)plainText[i]);
    }
    bytesConstRef bsConst(&bs);
    BOOST_CHECK(sha256(bsConst) == cipherText);
}

BOOST_AUTO_TEST_CASE(testRipemd160)
{
    const std::string plainText = "123456ABC+";
    h160 cipherText("0x74204bedd818292adc1127f9bb24bafd75468b62");
    bytes bs;
    for (size_t i = 0; i < plainText.length(); i++)
    {
        bs.push_back((byte)plainText[i]);
    }
    bytesConstRef bsConst(&bs);
    BOOST_CHECK(ripemd160(bsConst) == cipherText);
}
BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace bcos