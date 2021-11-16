hunter_config(bcos-framework VERSION 3.0.0-local
	URL https://${URL_BASE}/FISCO-BCOS/bcos-framework/archive/ba05b4b274d16d218108e271414ece026429b228.tar.gz
	SHA1 d8f6d4d9bc948b162775279c860cd1a51f044d1b
	CMAKE_ARGS HUNTER_PACKAGE_LOG_BUILD=ON HUNTER_PACKAGE_LOG_INSTALL=ON HUNTER_KEEP_PACKAGE_SOURCES=ON
)

hunter_config(wedpr-crypto VERSION 1.1.0-20f314df
	URL https://${URL_BASE}/WeBankBlockchain/WeDPR-Lab-Crypto/archive/10f314de45ec31ce9e330922b522ce173662ed33.tar.gz
	SHA1 626df59f87ea2c6bb5128f7d104588179809910b
	CMAKE_ARGS HUNTER_PACKAGE_LOG_BUILD=OFF HUNTER_PACKAGE_LOG_INSTALL=ON
)

hunter_config(bcos-crypto VERSION 3.0.1-local
	URL https://${URL_BASE}/FISCO-BCOS/bcos-crypto/archive/25c8edb7d5cbadb514bbce9733573c8ffdb3600d.tar.gz
	SHA1 4a1649e7095f5db58a5ae0671b2278bcccc25f1d
	CMAKE_ARGS HUNTER_PACKAGE_LOG_BUILD=OFF HUNTER_PACKAGE_LOG_INSTALL=ON
)

hunter_config(evmc VERSION 7.3.0-d7feb13f
	URL https://${URL_BASE}/FISCO-BCOS/evmc/archive/c7feb13f582919242da9f4f898ed4578785c9ecc.tar.gz
	SHA1 28ab1c74dd3340efe101418fd5faf19d34c9f7a9
)

hunter_config(intx VERSION 0.4.1 URL https://${URL_BASE}/chfast/intx/archive/v0.4.0.tar.gz
    SHA1 8a2a0b0efa64899db972166a9b3568a6984c61bc
	CMAKE_ARGS CMAKE_CXX_FLAGS=-std=c++17)

hunter_config(ethash VERSION 0.7.0-4576af36 URL https://${URL_BASE}/chfast/ethash/archive/4576af36f8ebb9bee2d5f04be692f295c64a7211.tar.gz
	SHA1 2001a265177c722b4cbe91c4160f3f582e0c9938
	CMAKE_ARGS CMAKE_CXX_FLAGS=-std=c++17)

hunter_config(evmone VERSION 0.4.1-b726a1e1
	URL https://${URL_BASE}/FISCO-BCOS/evmone/archive/b726a1e1722109291ac18bc7c5fad5aac9d2e8c5.tar.gz
	SHA1 e41fe0514a7a49a9a5e7eeb1b42cf2c3ced67f5d
	CMAKE_ARGS CMAKE_CXX_FLAGS=-std=c++17 BUILD_SHARED_LIBS=OFF
)
