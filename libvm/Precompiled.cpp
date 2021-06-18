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
 * @brief evm precompiled
 * @file Precompiled.cpp
 * @date: 2021-05-24
 */

#include "../libvm/Precompiled.h"
#include "../libstate/StateInterface.h"
#include "Hash.h"
#include "Common.h"
#include "ECRecover.h"
#include "wedpr-crypto/WedprBn128.h"
#include "wedpr-crypto/WedprCrypto.h"

using namespace std;
using namespace bcos;
using namespace bcos::crypto;

namespace bcos
{
namespace executor
{
PrecompiledRegistrar* PrecompiledRegistrar::s_this = nullptr;

PrecompiledExecutor const& PrecompiledRegistrar::executor(std::string const& _name)
{
    if (!get()->m_execs.count(_name))
        BOOST_THROW_EXCEPTION(ExecutorNotFound());
    return get()->m_execs[_name];
}

PrecompiledPricer const& PrecompiledRegistrar::pricer(std::string const& _name)
{
    if (!get()->m_pricers.count(_name))
        BOOST_THROW_EXCEPTION(PricerNotFound());
    return get()->m_pricers[_name];
}

}  // namespace executor
}  // namespace bcos

namespace
{
ETH_REGISTER_PRECOMPILED(ecrecover)(bytesConstRef _in)
{
    // When supported_version> = v2.4.0, ecRecover uniformly calls the ECDSA verification function
    return bcos::crypto::ecRecover(_in);
}

ETH_REGISTER_PRECOMPILED(sha256)(bytesConstRef _in)
{
    return {true, bcos::crypto::sha256(_in).asBytes()};
}

ETH_REGISTER_PRECOMPILED(ripemd160)(bytesConstRef _in)
{
    return {true, h256(bcos::crypto::ripemd160(_in), h256::AlignRight).asBytes()};
}

ETH_REGISTER_PRECOMPILED(identity)(bytesConstRef _in)
{
    return {true, _in.toBytes()};
}

// Parse _count bytes of _in starting with _begin offset as big endian int.
// If there's not enough bytes in _in, consider it infinitely right-padded with zeroes.
bigint parseBigEndianRightPadded(bytesConstRef _in, bigint const& _begin, bigint const& _count)
{
    if (_begin > _in.count())
        return 0;
    assert(_count <= numeric_limits<size_t>::max() / 8);  // Otherwise, the return value would not
                                                          // fit in the memory.

    size_t const begin{_begin};
    size_t const count{_count};

    // crop _in, not going beyond its size
    bytesConstRef cropped = _in.getCroppedData(begin, min(count, _in.count() - begin));

    bigint ret = fromBigEndian<bigint>(cropped);
    // shift as if we had right-padding zeroes
    assert(count - cropped.count() <= numeric_limits<size_t>::max() / 8);
    ret <<= 8 * (count - cropped.count());

    return ret;
}

ETH_REGISTER_PRECOMPILED(modexp)(bytesConstRef _in)
{
    // This is a protocol of bignumber modular
    // Described here:
    // https://github.com/ethereum/EIPs/blob/master/EIPS/eip-198.md
    bigint const baseLength(parseBigEndianRightPadded(_in, 0, 32));
    bigint const expLength(parseBigEndianRightPadded(_in, 32, 32));
    bigint const modLength(parseBigEndianRightPadded(_in, 64, 32));
    assert(modLength <= numeric_limits<size_t>::max() / 8);   // Otherwise gas should be too
                                                              // expensive.
    assert(baseLength <= numeric_limits<size_t>::max() / 8);  // Otherwise, gas should be too
                                                              // expensive.
    if (modLength == 0 && baseLength == 0)
        return {true, bytes{}};  // This is a special case where expLength can be very big.
    assert(expLength <= numeric_limits<size_t>::max() / 8);

    bigint const base(parseBigEndianRightPadded(_in, 96, baseLength));
    bigint const exp(parseBigEndianRightPadded(_in, 96 + baseLength, expLength));
    bigint const mod(parseBigEndianRightPadded(_in, 96 + baseLength + expLength, modLength));

    bigint const result = mod != 0 ? boost::multiprecision::powm(base, exp, mod) : bigint{0};

    size_t const retLength(modLength);
    bytes ret(retLength);
    toBigEndian(result, ret);

    return {true, ret};
}

namespace
{
bigint expLengthAdjust(bigint const& _expOffset, bigint const& _expLength, bytesConstRef _in)
{
    if (_expLength <= 32)
    {
        bigint const exp(parseBigEndianRightPadded(_in, _expOffset, _expLength));
        return exp ? msb(exp) : 0;
    }
    else
    {
        bigint const expFirstWord(parseBigEndianRightPadded(_in, _expOffset, 32));
        size_t const highestBit(expFirstWord ? msb(expFirstWord) : 0);
        return 8 * (_expLength - 32) + highestBit;
    }
}

bigint multComplexity(bigint const& _x)
{
    if (_x <= 64)
        return _x * _x;
    if (_x <= 1024)
        return (_x * _x) / 4 + 96 * _x - 3072;
    else
        return (_x * _x) / 16 + 480 * _x - 199680;
}
}  // namespace

ETH_REGISTER_PRECOMPILED_PRICER(modexp)(bytesConstRef _in)
{
    bigint const baseLength(parseBigEndianRightPadded(_in, 0, 32));
    bigint const expLength(parseBigEndianRightPadded(_in, 32, 32));
    bigint const modLength(parseBigEndianRightPadded(_in, 64, 32));

    bigint const maxLength(max(modLength, baseLength));
    bigint const adjustedExpLength(expLengthAdjust(baseLength + 96, expLength, _in));

    return multComplexity(maxLength) * max<bigint>(adjustedExpLength, 1) / 20;
}

ETH_REGISTER_PRECOMPILED(alt_bn128_G1_add)(bytesConstRef _in)
{
    pair<bool, bytes> ret{false, bytes(64, 0)};
    CInputBuffer in{(const char*)_in.data(), _in.size()};
    COutputBuffer result{(char*)ret.second.data(), 64};
    if (wedpr_fb_alt_bn128_g1_add(&in, &result) != 0)
    {
        return ret;
    }
    ret.first = true;
    return ret;
}

ETH_REGISTER_PRECOMPILED(alt_bn128_G1_mul)(bytesConstRef _in)
{
    pair<bool, bytes> ret{false, bytes(64, 0)};
    CInputBuffer in{(const char*)_in.data(), _in.size()};
    COutputBuffer result{(char*)ret.second.data(), 64};
    if (wedpr_fb_alt_bn128_g1_mul(&in, &result) != 0)
    {
        return ret;
    }
    ret.first = true;
    return ret;
}

ETH_REGISTER_PRECOMPILED(alt_bn128_pairing_product)(bytesConstRef _in)
{
    // Input: list of pairs of G1 and G2 points
    // Output: 1 if pairing evaluates to 1, 0 otherwise (left-padded to 32 bytes)
    pair<bool, bytes> ret{false, bytes(32, 0)};
    size_t constexpr pairSize = 2 * 32 + 2 * 64;
    size_t const pairs = _in.size() / pairSize;
    if (pairs * pairSize != _in.size())
    {
        // Invalid length.
        return ret;
    }

    CInputBuffer in{(const char*)_in.data(), _in.size()};
    COutputBuffer result{(char*)ret.second.data(), 32};
    if (wedpr_fb_alt_bn128_pairing_product(&in, &result) != 0)
    {
        return ret;
    }
    ret.first = true;
    return ret;
}

ETH_REGISTER_PRECOMPILED_PRICER(alt_bn128_pairing_product)
(bytesConstRef _in)
{
    auto const k = _in.size() / 192;
    return 45000 + k * 34000;
}

ETH_REGISTER_PRECOMPILED(blake2_compression)(bytesConstRef _in)
{
    static constexpr size_t roundsSize = 4;
    static constexpr size_t stateVectorSize = 8 * 8;
    static constexpr size_t messageBlockSize = 16 * 8;
    static constexpr size_t offsetCounterSize = 8;
    static constexpr size_t finalBlockIndicatorSize = 1;
    static constexpr size_t totalInputSize = roundsSize + stateVectorSize + messageBlockSize +
                                             2 * offsetCounterSize + finalBlockIndicatorSize;

    if (_in.size() != totalInputSize)
        return {false, {}};

    auto const rounds = fromBigEndian<uint32_t>(_in.getCroppedData(0, roundsSize));
    auto const stateVector = _in.getCroppedData(roundsSize, stateVectorSize);
    auto const messageBlockVector =
        _in.getCroppedData(roundsSize + stateVectorSize, messageBlockSize);
    auto const offsetCounter0 =
        _in.getCroppedData(roundsSize + stateVectorSize + messageBlockSize, offsetCounterSize);
    auto const offsetCounter1 = _in.getCroppedData(
        roundsSize + stateVectorSize + messageBlockSize + offsetCounterSize, offsetCounterSize);
    uint8_t const finalBlockIndicator =
        _in[roundsSize + stateVectorSize + messageBlockSize + 2 * offsetCounterSize];

    if (finalBlockIndicator != 0 && finalBlockIndicator != 1)
        return {false, {}};

    return {true, bcos::crypto::blake2FCompression(rounds, stateVector, offsetCounter0,
                      offsetCounter1, finalBlockIndicator, messageBlockVector)};
}

ETH_REGISTER_PRECOMPILED_PRICER(blake2_compression)
(bytesConstRef _in)
{
    auto const rounds = fromBigEndian<uint32_t>(_in.getCroppedData(0, 4));
    return rounds;
}


}  // namespace

namespace bcos
{
namespace precompiled
{
storage::TableInterface::Ptr Precompiled::createTable(
    storage::TableFactoryInterface::Ptr _tableFactory, const std::string& tableName,
    const std::string& keyField, const std::string& valueField)
{
    auto ret = _tableFactory->createTable(tableName, keyField, valueField);
    if (!ret)
    {
        return nullptr;
    }
    else
    {
        return _tableFactory->openTable(tableName);
    }
}

bool Precompiled::checkAuthority(storage::TableFactoryInterface::Ptr _tableFactory,
    const std::string& _origin, const std::string& _contract)
{
    auto tableName = executor::getContractTableName(_contract);
    return _tableFactory->checkAuthority(tableName, _origin);
}
}  // namespace precompiled
}  // namespace bcos