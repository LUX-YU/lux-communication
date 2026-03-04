find_library(JEMALLOC_LIB jemalloc REQUIRED)

add_executable(node_test node_test.cpp)
target_link_libraries(node_test PRIVATE lux::communication::node ${JEMALLOC_LIB})

add_executable(node_simple_test simple_test.cpp)
target_link_libraries(node_simple_test PRIVATE lux::communication::node)

add_executable(seq_order_test seq_order_test.cpp)
target_link_libraries(seq_order_test PRIVATE lux::communication::node)

add_executable(discovery_test discovery_test.cpp)
target_link_libraries(discovery_test PRIVATE lux::communication::node)
target_include_directories(discovery_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../pinclude)

add_executable(shm_transport_test shm_transport_test.cpp)
target_link_libraries(shm_transport_test PRIVATE lux::communication::node)
target_include_directories(shm_transport_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../pinclude)

add_executable(loopback_optimization_test loopback_optimization_test.cpp)
target_link_libraries(loopback_optimization_test PRIVATE lux::communication::node)
target_include_directories(loopback_optimization_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../pinclude)

add_executable(net_transport_test net_transport_test.cpp)
target_link_libraries(net_transport_test PRIVATE lux::communication::node)
target_include_directories(net_transport_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../pinclude)

add_executable(unified_transport_test unified_transport_test.cpp)
target_link_libraries(unified_transport_test PRIVATE lux::communication::node)
target_include_directories(unified_transport_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../pinclude)

add_executable(qos_test qos_test.cpp)
target_link_libraries(qos_test PRIVATE lux::communication::node)
target_include_directories(qos_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../pinclude)
