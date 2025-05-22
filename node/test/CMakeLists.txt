add_executable(node_test node_test.cpp)
target_link_libraries(node_test PRIVATE lux::communication::node)

add_executable(timeorder_executor_test timeorder_executor_test.cpp)
target_link_libraries(timeorder_executor_test PRIVATE lux::communication::node)



add_executable(executor_interprocess_test executor_interprocess_test.cpp)
target_link_libraries(executor_interprocess_test PRIVATE lux::communication::node)

add_executable(communication_intra_inter_test communication_intra_inter_test.cpp)
target_link_libraries(communication_intra_inter_test PRIVATE lux::communication::node)
