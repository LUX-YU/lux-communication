find_library(JEMALLOC_LIB jemalloc REQUIRED)

add_executable(node_test node_test.cpp)
target_link_libraries(node_test PRIVATE lux::communication::node ${JEMALLOC_LIB})

add_executable(node_simple_test simple_test.cpp)
target_link_libraries(node_simple_test PRIVATE lux::communication::node)
