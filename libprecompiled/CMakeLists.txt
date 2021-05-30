file(GLOB_RECURSE SRC_LIST "*.cpp")
file(GLOB_RECURSE HEADERS "*.h")

hunter_add_package(jsoncpp)
find_package(jsoncpp CONFIG REQUIRED)

hunter_add_package(bcos-crypto)
find_package(bcos-crypto CONFIG QUIET REQUIRED)
add_library(precompiled ${SRC_LIST} ${HEADERS})
target_link_libraries(precompiled PUBLIC bcos-framework::utilities bcos-crypto jsoncpp_lib_static)
add_subdirectory(extension)