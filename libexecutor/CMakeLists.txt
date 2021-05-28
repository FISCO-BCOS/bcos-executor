set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(executor ${SRC_LIST} ${HEADERS})

target_include_directories(executor PRIVATE ..)
target_link_libraries(executor PUBLIC precompiled vm state)
