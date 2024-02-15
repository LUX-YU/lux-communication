include_component_cmake_scripts(lux::communication::protobuf)

set(PROTO_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/protogen)
set(BUILTIN_MSGS_PROTO_FILES
	${CMAKE_CURRENT_SOURCE_DIR}/msgs/lux/common_msgs/timestamp.proto
	${CMAKE_CURRENT_SOURCE_DIR}/msgs/lux/sensor_msgs/binocular_image.proto
	${CMAKE_CURRENT_SOURCE_DIR}/msgs/lux/sensor_msgs/image.proto
)

generate_protos(
	PROTOS				${BUILTIN_MSGS_PROTO_FILES}
    PROTOC_OUT_DIR      ${PROTO_GEN_DIR}
    EXPORT_MACRO        LUX_COMMUNICATION_PUBLIC
    INCLUDE_HEADERS     lux/communication/visibility.h
	SOURCE_FILES_OUTPUT PROTO_SOURCES_FILES
	IMPORT_DIRS			${CMAKE_CURRENT_SOURCE_DIR}/msgs
)

add_component(
	COMPONENT_NAME	builtin_msgs
	NAMESPACE		lux::communication
	SOURCE_FILES	${PROTO_SOURCES_FILES}
)

component_include_directories(
	builtin_msgs
	BUILD_TIME_SHARED
		${PROTO_GEN_DIR}
	BUILD_TIME_EXPORT
		${LUX_GENERATE_HEADER_DIR}
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

target_compile_definitions(
	builtin_msgs
	PRIVATE
	LUX_COMMUNICATION_LIBRARY
)

install(
	DIRECTORY	${PROTO_GEN_DIR}/lux
	DESTINATION	${CMAKE_INSTALL_PREFIX}/include
	PATTERN		PATTERN "*.cc" EXCLUDE
)
