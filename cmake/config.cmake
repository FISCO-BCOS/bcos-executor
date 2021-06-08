
# hunter_config(Boost VERSION 1.76.0)
hunter_config(bcos-framework VERSION 3.0.0-40db9d76eb937f417f124ab2dbcbe6ab7981839e
	URL https://${URL_BASE}/FISCO-BCOS/bcos-framework/archive/40db9d76eb937f417f124ab2dbcbe6ab7981839e.tar.gz
    SHA1 b6e2be6f12c4fe3ff9ffb1f5fb9b32e0fcfaf98f
    CMAKE_ARGS HUNTER_PACKAGE_LOG_BUILD=ON HUNTER_PACKAGE_LOG_INSTALL=ON
)

hunter_config(bcos-crypto VERSION 3.0.0-e9347d146c0eea62ed4c4ce87d04d8569ef3511c
		URL https://${URL_BASE}/FISCO-BCOS/bcos-crypto/archive/e9347d146c0eea62ed4c4ce87d04d8569ef3511c.tar.gz
		SHA1 f48df7c3e854e35f6e12496ab415b4f6a943b800
		CMAKE_ARGS HUNTER_PACKAGE_LOG_BUILD=ON HUNTER_PACKAGE_LOG_INSTALL=ON
)
