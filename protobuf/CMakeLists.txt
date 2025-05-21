find_package(Protobuf CONFIG REQUIRED)

add_interface_component(
	COMPONENT_NAME 	protobuf
	NAMESPACE 		lux::communication
)

component_add_cmake_scripts(
	protobuf
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/component_proto_tools.cmake
)

component_add_transitive_commands(
	protobuf
	"find_package(Protobuf REQUIRED)"
)

target_link_libraries(
	protobuf
	INTERFACE
	protobuf::libprotoc 
	protobuf::libprotobuf 
	protobuf::libprotobuf-lite
)