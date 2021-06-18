
# hunter_config(Boost VERSION 1.76.0)
# hunter_config(wedpr-crypto VERSION 1.1.0-5fd2ab0a
# 	URL https://${URL_BASE}/WeBankBlockchain/WeDPR-Lab-Crypto/archive/5fd2ab0a0aed570e9fc7d7af6ee5aed89dab2739.tar.gz
# 	SHA1 d0834d74d2308c4cfa2c9737dc3dcb627cb3dfee)
hunter_config(bcos-framework VERSION 3.0.0-7fb5f25546a030488057373bab3918985657b079
	URL https://${URL_BASE}/FISCO-BCOS/bcos-framework/archive/7fb5f25546a030488057373bab3918985657b079.tar.gz
    SHA1 588bb72138fb71138262e5e2304e24f3ddebbf2c
    CMAKE_ARGS HUNTER_PACKAGE_LOG_BUILD=ON HUNTER_PACKAGE_LOG_INSTALL=ON
)

hunter_config(bcos-crypto VERSION 3.0.0-f350ea889a0ad44b7efbd528d4829446b80e9665
		URL https://${URL_BASE}/FISCO-BCOS/bcos-crypto/archive/f350ea889a0ad44b7efbd528d4829446b80e9665.tar.gz
		SHA1 692989c6369d7085559f48264894b115241e8dc7
		CMAKE_ARGS HUNTER_PACKAGE_LOG_BUILD=ON HUNTER_PACKAGE_LOG_INSTALL=ON
)
