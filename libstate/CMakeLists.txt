file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(state ${SRC_LIST} ${HEADERS})

target_link_libraries(state PUBLIC bcos-framework::table)
