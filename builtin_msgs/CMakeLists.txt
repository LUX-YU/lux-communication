find_package(Stb	QUIET)
find_package(Eigen3 CONFIG REQUIRED)
if(NOT Stb_FOUND)
    find_path(Stb_INCLUDE_DIR stb_image.h REQUIRED 
        HINTS
            /usr/include 
            /usr/include/stb
            /usr/local
            /usr/local/include/stb
    )
endif()

include_component_cmake_scripts(lux::communication::protobuf)
set(PROTO_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/protogen)
set(BUILTIN_MSGS_PROTO_FILES
	${CMAKE_CURRENT_SOURCE_DIR}/msgs/lux/communication/builtin_msgs/common_msgs/timestamp.proto
	${CMAKE_CURRENT_SOURCE_DIR}/msgs/lux/communication/builtin_msgs/common_msgs/matrix3.proto
	${CMAKE_CURRENT_SOURCE_DIR}/msgs/lux/communication/builtin_msgs/common_msgs/matrixn.proto
	${CMAKE_CURRENT_SOURCE_DIR}/msgs/lux/communication/builtin_msgs/common_msgs/vector3.proto
	${CMAKE_CURRENT_SOURCE_DIR}/msgs/lux/communication/builtin_msgs/common_msgs/vectorn.proto
	${CMAKE_CURRENT_SOURCE_DIR}/msgs/lux/communication/builtin_msgs/geometry_msgs/quaternion.proto
	${CMAKE_CURRENT_SOURCE_DIR}/msgs/lux/communication/builtin_msgs/geometry_msgs/point.proto
	${CMAKE_CURRENT_SOURCE_DIR}/msgs/lux/communication/builtin_msgs/geometry_msgs/pose.proto
	${CMAKE_CURRENT_SOURCE_DIR}/msgs/lux/communication/builtin_msgs/geometry_msgs/accel.proto
	${CMAKE_CURRENT_SOURCE_DIR}/msgs/lux/communication/builtin_msgs/geometry_msgs/twist.proto
	${CMAKE_CURRENT_SOURCE_DIR}/msgs/lux/communication/builtin_msgs/sensor_msgs/image.proto
	${CMAKE_CURRENT_SOURCE_DIR}/msgs/lux/communication/builtin_msgs/sensor_msgs/image_stamped.proto
	${CMAKE_CURRENT_SOURCE_DIR}/msgs/lux/communication/builtin_msgs/sensor_msgs/image_group.proto
	${CMAKE_CURRENT_SOURCE_DIR}/msgs/lux/communication/builtin_msgs/sensor_msgs/image_group_stamped.proto
	${CMAKE_CURRENT_SOURCE_DIR}/msgs/lux/communication/builtin_msgs/sensor_msgs/imu.proto
	${CMAKE_CURRENT_SOURCE_DIR}/msgs/lux/communication/builtin_msgs/sensor_msgs/imu_stamped.proto
	${CMAKE_CURRENT_SOURCE_DIR}/msgs/lux/communication/builtin_msgs/sensor_msgs/navsat.proto
	${CMAKE_CURRENT_SOURCE_DIR}/msgs/lux/communication/builtin_msgs/sensor_msgs/navsat_stamped.proto
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
					${CMAKE_CURRENT_SOURCE_DIR}/src/load_image.cpp
					${CMAKE_CURRENT_SOURCE_DIR}/src/timestamp_tools.cpp
)

component_include_directories(
	builtin_msgs
	BUILD_TIME_SHARED
		${PROTO_GEN_DIR}
	BUILD_TIME_EXPORT
		${CMAKE_CURRENT_SOURCE_DIR}/include
		${LUX_GENERATE_HEADER_DIR}
	PRIVATE
        ${Stb_INCLUDE_DIR}
)

component_add_internal_dependencies(
	builtin_msgs
	lux::communication::protobuf
)

target_link_libraries(
	builtin_msgs
	PUBLIC
	Eigen3::Eigen
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

set(ENABLE_BUILTIN_MSGS_TEST ON CACHE BOOL  "Enable builtin message test")
if(ENABLE_BUILTIN_MSGS_TEST)
	add_subdirectory(test)
endif()

# TODO simplify this
install(
	DIRECTORY	${PROTO_GEN_DIR}/lux
	DESTINATION	${CMAKE_INSTALL_PREFIX}/include
	PATTERN		PATTERN "*.cc" EXCLUDE
)
