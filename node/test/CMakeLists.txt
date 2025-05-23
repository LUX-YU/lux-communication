enable_testing()

add_executable(node_test node_test.cpp)
target_link_libraries(node_test PRIVATE lux::communication::node)
add_test(NAME node_test COMMAND node_test)

add_executable(timeorder_executor_test timeorder_executor_test.cpp)
target_link_libraries(timeorder_executor_test PRIVATE lux::communication::node)
add_test(NAME timeorder_executor_test COMMAND timeorder_executor_test)

add_executable(executor_interprocess_test executor_interprocess_test.cpp)
target_link_libraries(executor_interprocess_test PRIVATE lux::communication::node)
add_test(NAME executor_interprocess_test COMMAND executor_interprocess_test)

add_executable(communication_intra_inter_test communication_intra_inter_test.cpp)
target_link_libraries(communication_intra_inter_test PRIVATE lux::communication::node)
add_test(NAME communication_intra_inter_test COMMAND communication_intra_inter_test)

add_executable(discovery_test discovery_test.cpp)
target_link_libraries(discovery_test PRIVATE lux::communication::node)
add_test(NAME discovery_test COMMAND discovery_test)

add_executable(interprocess_performance_test interprocess_performance_test.cpp)
target_link_libraries(interprocess_performance_test PRIVATE lux::communication::node)
# Performance test is not added to ctest by default to keep test time reasonable

add_executable(interprocess_spin_test interprocess_spin_test.cpp)
target_link_libraries(interprocess_spin_test PRIVATE lux::communication::node)
add_test(NAME interprocess_spin_test COMMAND interprocess_spin_test)
