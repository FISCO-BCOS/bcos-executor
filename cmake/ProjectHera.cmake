include(ExternalProject)
include(GNUInstallDirs)
find_program(CARGO_COMMAND cargo)
find_program(RUSTUP_COMMAND rustup)
if(NOT CARGO_COMMAND OR NOT RUSTUP_COMMAND)
    message(FATAL_ERROR "cargo/rustup is not installed")
endif()

execute_process(COMMAND rustup show OUTPUT_VARIABLE RUSTC_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
set(RUSTC_VERSION_REQUIRED "1.47.0")
string(FIND ${RUSTC_VERSION} ${RUSTC_VERSION_REQUIRED} RUSTC_OK)
if (${RUSTC_OK} EQUAL -1)
    message(STATUS "${RUSTC_VERSION}")
    message(STATUS "rustc ${RUSTC_VERSION_REQUIRED} is not installed, executing `rustup toolchain install ${RUSTC_VERSION_REQUIRED}` to install it.")
    execute_process(COMMAND rustup toolchain install ${RUSTC_VERSION_REQUIRED})
endif()
execute_process(COMMAND rustup override set ${RUSTC_VERSION_REQUIRED} --path ${CMAKE_SOURCE_DIR}/deps/src/hera OUTPUT_QUIET ERROR_QUIET)

set(USE_WASMER OFF)
if(USE_WASMER)
    set(USE_WASMTIME OFF)
    set(WASM_ENGINE_LIBRARY "wasmer_c_api")
else()
    set(USE_WASMTIME ON)
    set(WASM_ENGINE_LIBRARY "wasmtime")
endif()

ExternalProject_Add(hera
        PREFIX ${CMAKE_SOURCE_DIR}/deps
        DOWNLOAD_NO_PROGRESS 1
        GIT_REPOSITORY https://github.com/FISCO-BCOS/hera.git
        GIT_SHALLOW false
        GIT_TAG 06eec7873a8df1f2b233dfa22faff30822e5691b
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
                   -DBUILD_SHARED_LIBS=OFF
                   -DHERA_WASMTIME=${USE_WASMTIME}
                   -DHERA_WASMER=${USE_WASMER}
                   -DHERA_WASMER_NATIVE_ENGINE=OFF
                   -DHERA_WASMER_LLVM_BACKEND=OFF
                   -DHERA_DEBUGGING=${DEBUG}
                   -DEVMC_ROOT=<INSTALL_DIR>
                   -DHUNTER_STATUS_DEBUG=ON
                   -DHUNTER_USE_CACHE_SERVERS=NO
        BUILD_IN_SOURCE 1
        # BUILD_COMMAND cmake --build . -- -j
        LOG_DOWNLOAD 1
        LOG_CONFIGURE 1
        LOG_BUILD 1
        LOG_INSTALL 1
        BUILD_BYPRODUCTS <INSTALL_DIR>/lib/libevmone.a <INSTALL_DIR>/lib/libhera-buildinfo.a <INSTALL_DIR>/lib/lib${WASM_ENGINE_LIBRARY}.a
)

ExternalProject_Get_Property(hera INSTALL_DIR)
set(HERA_INCLUDE_DIRS ${INSTALL_DIR}/include)
file(MAKE_DIRECTORY ${HERA_INCLUDE_DIRS})  # Must exist.
set(HERA_LIBRARIES ${INSTALL_DIR}/lib/libhera.a ${INSTALL_DIR}/lib/libhera-buildinfo.a ${INSTALL_DIR}/lib/lib${WASM_ENGINE_LIBRARY}.a)
if(DEBUG)
    set(HERA_LIBRARIES ${HERA_LIBRARIES} ${EVMC_INSTRUCTIONS_LIBRARIES})
endif()
if(NOT APPLE)
    set(HERA_LIBRARIES ${HERA_LIBRARIES} rt)
endif()
add_library(HERA INTERFACE IMPORTED)
set_property(TARGET HERA PROPERTY INTERFACE_LINK_LIBRARIES ${HERA_LIBRARIES})
set_property(TARGET HERA PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${HERA_INCLUDE_DIRS})
add_dependencies(hera EVMC)
add_dependencies(HERA hera)
