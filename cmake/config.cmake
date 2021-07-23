
# hunter_config(Boost VERSION 1.76.0)
# hunter_config(wedpr-crypto VERSION 1.1.0-5fd2ab0a
# 	URL https://${URL_BASE}/WeBankBlockchain/WeDPR-Lab-Crypto/archive/5fd2ab0a0aed570e9fc7d7af6ee5aed89dab2739.tar.gz
# 	SHA1 d0834d74d2308c4cfa2c9737dc3dcb627cb3dfee)
hunter_config(bcos-framework VERSION 3.0.0-ca941668
	URL https://${URL_BASE}/FISCO-BCOS/bcos-framework/archive/17a5d4ae70b28bf0661a809e34c901c28d1f5ee3.tar.gz
	SHA1 900ee21f3cec83aeea5fc44e7a794d59dd907b34
    CMAKE_ARGS HUNTER_PACKAGE_LOG_BUILD=ON HUNTER_PACKAGE_LOG_INSTALL=ON
)

hunter_config(bcos-crypto VERSION 3.0.0-local
	URL https://${URL_BASE}/FISCO-BCOS/bcos-crypto/archive/25c8edb7d5cbadb514bbce9733573c8ffdb3600d.tar.gz
	SHA1 4a1649e7095f5db58a5ae0671b2278bcccc25f1d
	CMAKE_ARGS HUNTER_PACKAGE_LOG_BUILD=ON HUNTER_PACKAGE_LOG_INSTALL=ON
)

hunter_config(evmc VERSION 7.3.0-d951b1ef
	URL https://${URL_BASE}/FISCO-BCOS/evmc/archive/d951b1ef088be6922d80f41c3c83c0cbd69d2bfa.tar.gz
	SHA1 0b39b36cd8533c89ee0b15b6e94cff1111383ac7
)

hunter_config(intx VERSION 0.4.1 URL https://github.com/chfast/intx/archive/v0.4.0.tar.gz
    SHA1 8a2a0b0efa64899db972166a9b3568a6984c61bc
	CMAKE_ARGS CMAKE_CXX_FLAGS=-std=c++17)

hunter_config(ethash VERSION 0.7.0-4576af36 URL https://${URL_BASE}/chfast/ethash/archive/4576af36f8ebb9bee2d5f04be692f295c64a7211.tar.gz
	SHA1 2001a265177c722b4cbe91c4160f3f582e0c9938
	CMAKE_ARGS CMAKE_CXX_FLAGS=-std=c++17)

hunter_config(evmone VERSION 0.4.1-9eb137ea
	URL https://${URL_BASE}/FISCO-BCOS/evmone/archive/ea98055f1ebd216cdc89679ae64005e06aa620ae.tar.gz
	SHA1 8d3d499a2db398ca339ece20fd34ed463454b367
	CMAKE_ARGS CMAKE_CXX_FLAGS=-std=c++17 BUILD_SHARED_LIBS=OFF
)
