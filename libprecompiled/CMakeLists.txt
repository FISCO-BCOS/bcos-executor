file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

hunter_add_package(jsoncpp)
find_package(jsoncpp CONFIG REQUIRED)

add_library(precompiled ${SRC_LIST} ${HEADERS})
target_link_libraries(precompiled PUBLIC bcos-framework::utilities jsoncpp_lib_static)