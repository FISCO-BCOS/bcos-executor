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
*/

#include "SHA256.h"
#include <secp256k1_sha256.h>

using namespace std;

namespace bcos
{
namespace crypto
{
// add sha2 -- sha256 to this file begin
h256 sha256(bytesConstRef _input) noexcept
{
    secp256k1_sha256_t ctx;
    secp256k1_sha256_initialize(&ctx);
    secp256k1_sha256_write(&ctx, _input.data(), _input.size());
    h256 hash;
    secp256k1_sha256_finalize(&ctx, hash.data());
    return hash;
}
}

}  // namespace dev