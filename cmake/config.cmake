
# hunter_config(Boost VERSION 1.76.0)
# hunter_config(wedpr-crypto VERSION 1.1.0-5fd2ab0a
# 	URL https://${URL_BASE}/WeBankBlockchain/WeDPR-Lab-Crypto/archive/5fd2ab0a0aed570e9fc7d7af6ee5aed89dab2739.tar.gz
# 	SHA1 d0834d74d2308c4cfa2c9737dc3dcb627cb3dfee)
hunter_config(bcos-framework VERSION 3.0.0-local
	URL https://${URL_BASE}/FISCO-BCOS/bcos-framework/archive/0206314c246ed45a76c772c9a9d1e6382c99a081.tar.gz
	SHA1 b40a1dbca553624317fec35200f69847f7a17ce1
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
