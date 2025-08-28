add_executable(
	conversion_test
	conversion_test.cpp
)

target_link_libraries(
	conversion_test
	PRIVATE
	builtin_msgs
	builtin_msgs_protos
)