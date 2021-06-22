file(GLOB_RECURSE SRC_LIST "*.cpp")
file(GLOB_RECURSE HEADERS "*.h")

hunter_add_package(jsoncpp)
find_package(jsoncpp CONFIG REQUIRED)
find_package(Boost CONFIG REQUIRED serialization)

find_package(bcos-crypto CONFIG QUIET REQUIRED)
add_library(precompiled ${SRC_LIST} ${HEADERS})
target_link_libraries(precompiled PUBLIC bcos-framework::codec bcos-framework::utilities bcos-crypto::bcos-crypto jsoncpp_lib_static Boost::serialization)
target_link_libraries(precompiled PRIVATE vm)
add_subdirectory(extension)

# install(
#     TARGETS precompiled
#     EXPORT "${TARGETS_EXPORT_NAME}"
#     LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
#     ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
#     RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
#     INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
# )
install_dependencies(precompiled)

