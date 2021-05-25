file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(precompiled ${SRC_LIST} ${HEADERS})
target_link_libraries(precompiled PUBLIC bcos-framework::utilities)