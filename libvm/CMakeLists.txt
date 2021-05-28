file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(vm ${SRC_LIST} ${HEADERS})

# target_include_directories(executivecontext PRIVATE ${EVMC_INCLUDE_DIRS})
find_package(Boost CONFIG QUIET REQUIRED program_options)

target_link_libraries(vm PUBLIC EVMC tbb::tbb state metering Boost::program_options libff::ff Secp256k1 wedpr-crypto::crypto)
target_link_libraries(vm PRIVATE EVMONE HERA)
target_compile_options(vm PRIVATE -Wno-error)
