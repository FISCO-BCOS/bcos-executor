file(GLOB_RECURSE SRC_LIST "*.cpp")
file(GLOB_RECURSE HEADERS "*.h")

hunter_add_package(jsoncpp)
find_package(jsoncpp CONFIG REQUIRED)

find_package(bcos-crypto CONFIG QUIET REQUIRED)
add_library(precompiled ${SRC_LIST} ${HEADERS})
target_link_libraries(precompiled PUBLIC vm bcos-framework::utilities bcos-crypto::bcos-crypto jsoncpp_lib_static)
add_subdirectory(extension)
