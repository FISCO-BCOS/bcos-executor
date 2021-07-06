file(GLOB_RECURSE SRC_LIST "*.cpp")
file(GLOB_RECURSE HEADERS "*.h")

hunter_add_package(evmc)
find_package(evmc CONFIG REQUIRED)
hunter_add_package(intx)
find_package(intx CONFIG REQUIRED)
hunter_add_package(ethash)
find_package(ethash CONFIG REQUIRED)
hunter_add_package(evmone)
find_package(evmone CONFIG REQUIRED)

add_library(vm ${SRC_LIST} ${HEADERS})

# target_include_directories(executivecontext PRIVATE ${EVMC_INCLUDE_DIRS})
find_package(Boost CONFIG QUIET REQUIRED program_options)

target_link_libraries(vm PUBLIC tbb::tbb state bcos-framework::protocol bcos-framework::utilities
                         Boost::program_options wedpr-crypto::crypto wedpr-crypto::extend-crypto
                         evmone hera hera-buildinfo ${WASM_ENGINE_LIBRARY} evmc::loader evmc::instructions wabt)
target_compile_options(vm PRIVATE -Wno-error -fno-var-tracking)

install_dependencies(vm)
