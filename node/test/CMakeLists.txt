find_library(JEMALLOC_LIB jemalloc REQUIRED)

#[[
add_executable(node_test node_test.cpp)
target_link_libraries(node_test PRIVATE lux::communication::node ${JEMALLOC_LIB})

add_executable(timeorder_executor_test timeorder_executor_test.cpp)
target_link_libraries(timeorder_executor_test PRIVATE lux::communication::node)

add_executable(interprocess_subscriber interprocess_subscriber.cpp)
target_link_libraries(interprocess_subscriber PRIVATE lux::communication::node)

add_executable(interprocess_publisher interprocess_publisher.cpp)
target_link_libraries(interprocess_publisher PRIVATE lux::communication::node)
]]

add_executable(node_simple_test simple_test.cpp)
target_link_libraries(node_simple_test PRIVATE lux::communication::node)