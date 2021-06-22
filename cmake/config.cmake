
# hunter_config(Boost VERSION 1.76.0)
# hunter_config(wedpr-crypto VERSION 1.1.0-5fd2ab0a
# 	URL https://${URL_BASE}/WeBankBlockchain/WeDPR-Lab-Crypto/archive/5fd2ab0a0aed570e9fc7d7af6ee5aed89dab2739.tar.gz
# 	SHA1 d0834d74d2308c4cfa2c9737dc3dcb627cb3dfee)
hunter_config(bcos-framework VERSION 3.0.0-47b1fecd68e13598b9f73333446193c4c301e065
	URL https://${URL_BASE}/FISCO-BCOS/bcos-framework/archive/47b1fecd68e13598b9f73333446193c4c301e065.tar.gz
    SHA1 528b5b659258c4882716a30396789b1f162c1b02
    CMAKE_ARGS HUNTER_PACKAGE_LOG_BUILD=ON HUNTER_PACKAGE_LOG_INSTALL=ON
)

hunter_config(bcos-crypto VERSION 3.0.0-f350ea889a0ad44b7efbd528d4829446b80e9665
		URL https://${URL_BASE}/FISCO-BCOS/bcos-crypto/archive/f350ea889a0ad44b7efbd528d4829446b80e9665.tar.gz
		SHA1 692989c6369d7085559f48264894b115241e8dc7
		CMAKE_ARGS HUNTER_PACKAGE_LOG_BUILD=ON HUNTER_PACKAGE_LOG_INSTALL=ON
)
hunter_config(evmc VERSION 7.3.0-d951b1ef
		URL https://${URL_BASE}/FISCO-BCOS/evmc/archive/d951b1ef088be6922d80f41c3c83c0cbd69d2bfa.tar.gz
		SHA1 0b39b36cd8533c89ee0b15b6e94cff1111383ac7
)
