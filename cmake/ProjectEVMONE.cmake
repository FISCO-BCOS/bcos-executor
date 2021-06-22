include(ExternalProject)
include(GNUInstallDirs)

ExternalProject_Add(evmone_project
        PREFIX ${CMAKE_SOURCE_DIR}/deps
        DOWNLOAD_NO_PROGRESS 1
        DOWNLOAD_NAME evmone-37799f82.tar.gz
        URL https://${URL_BASE}/FISCO-BCOS/evmone/archive/37799f8226c72e2e64e59583f07ab834868de33e.tar.gz
        URL_HASH SHA256=46bcda09c0d2cd8977f628a4d57f068a7b1d0b0113099c72948c905c838dce3c
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
                   -DBUILD_SHARED_LIBS=off
                   -DEVMC_ROOT=${EVMC_ROOT}
                   -DHUNTER_ROOT=${CMAKE_SOURCE_DIR}/deps/src/.hunter
                   -DHUNTER_STATUS_DEBUG=ON
                   -DHUNTER_USE_CACHE_SERVERS=NO
        # BUILD_COMMAND cmake --build . -- -j
        BUILD_IN_SOURCE 1
        LOG_DOWNLOAD 1
        LOG_CONFIGURE 1
        LOG_BUILD 1
        LOG_INSTALL 1
        BUILD_BYPRODUCTS <INSTALL_DIR>/lib/libevmone.a
)

ExternalProject_Get_Property(evmone_project INSTALL_DIR)
set(EVMONE_INCLUDE_DIRS ${INSTALL_DIR}/include)
file(MAKE_DIRECTORY ${EVMONE_INCLUDE_DIRS})  # Must exist.
add_library(keccak STATIC IMPORTED)
set_property(TARGET keccak PROPERTY IMPORTED_LOCATION ${INSTALL_DIR}/lib/libkeccak.a)
add_library(intx STATIC IMPORTED)
set_property(TARGET intx PROPERTY IMPORTED_LOCATION ${INSTALL_DIR}/lib/libintx.a)
add_library(evmone STATIC IMPORTED)
set_property(TARGET evmone PROPERTY IMPORTED_LOCATION ${INSTALL_DIR}/lib/libevmone.a)
set_property(TARGET evmone PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${EVMONE_INCLUDE_DIRS})
# add_dependencies(evmone_project evmc)
add_dependencies(evmone evmone_project intx keccak)
