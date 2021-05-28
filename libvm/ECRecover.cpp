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
/** @file ECRecover.cpp
 * @author xingqiangbai
 * @date 2021-05-27
 */

#include "ECRecover.h"
#include "bcos-framework/interfaces/crypto/CommonType.h"
#include "wedpr-crypto/WedprCrypto.h"


using namespace std;
namespace bcos
{
namespace crypto
{
const int RSV_LENGTH = 65;
const int PUBLIC_KEY_LENGTH = 64;
pair<bool, bytes> ecRecover(bytesConstRef _in)
{  // _in is hash(32),v(32),r(32),s(32), return address
    byte rawRSV[RSV_LENGTH];
    memcpy(rawRSV, _in.data() + 64, RSV_LENGTH - 1);
    rawRSV[RSV_LENGTH - 1] = (byte)((int)_in[63] - 27);
    CInputBuffer msgHash{(const char*)_in.data(), crypto::HashType::size};
    CInputBuffer rsv{(const char*)rawRSV, RSV_LENGTH};

    pair<bool, bytes> ret{true, bytes(crypto::HashType::size, 0)};
    bytes publicKeyBytes(64, 0);
    COutputBuffer publicKey{(char*)publicKeyBytes.data(), PUBLIC_KEY_LENGTH};
    auto retCode = wedpr_secp256k1_recover_public_key(&msgHash, &rsv, &publicKey);
    if (retCode != 0)
    {
        return {true, {}};
    }
    // keccak256 and set first 12 byte to zero
    CInputBuffer pubkeyBuffer{(const char*)publicKeyBytes.data(), PUBLIC_KEY_LENGTH};
    COutputBuffer pubkeyHash{(char*)ret.second.data(), crypto::HashType::size};
    retCode = wedpr_keccak256_hash(&pubkeyBuffer, &pubkeyHash);
    if (retCode != 0)
    {
        return {true, {}};
    }
    memset(ret.second.data(), 0, 12);
    return ret;
}

}  // namespace crypto
}  // namespace bcos
