file(GLOB_RECURSE SRC_LIST "*.cpp")
file(GLOB_RECURSE HEADERS "*.h")

add_library(vm ${SRC_LIST} ${HEADERS})

# target_include_directories(executivecontext PRIVATE ${EVMC_INCLUDE_DIRS})
find_package(Boost CONFIG QUIET REQUIRED program_options)

target_link_libraries(vm PUBLIC EVMC tbb::tbb state bcos-framework::protocol bcos-framework::utilities Boost::program_options wedpr-crypto::crypto wedpr-crypto::extend-crypto WABT)
target_link_libraries(vm PRIVATE EVMONE HERA)
target_compile_options(vm PRIVATE -Wno-error -fno-var-tracking)

install(
    TARGETS vm
    EXPORT "${TARGETS_EXPORT_NAME}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
