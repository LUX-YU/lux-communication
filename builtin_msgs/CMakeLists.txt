include_component_cmake_scripts(lux::communication::protobuf)

add_interface_component(
	COMPONENT_NAME	builtin_msgs
	NAMESPACE		lux::communication
)

component_add_internal_dependencies(
	builtin_msgs
	lux::communication::protobuf
)

component_proto_include_directories(
	builtin_msgs
	BUILD_TIME
		${CMAKE_CURRENT_SOURCE_DIR}/msgs
	INSTALL_TIME
		msgs
)
