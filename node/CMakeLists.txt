find_package(cppzmq   CONFIG REQUIRED)
find_package(stduuid  CONFIG REQUIRED)
find_package(lux-cxx		 REQUIRED COMPONENTS concurrent compile_time)

set(LUX_COMMUNICATION_NODE_SRC
	${CMAKE_CURRENT_SOURCE_DIR}/src/Publisher.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/Subscriber.cpp
)

if(WIN32)
	LIST(APPEND LUX_COMMUNICATION_NODE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/UdpMultiCastImplWin.cpp)
else()
	LIST(APPEND LUX_COMMUNICATION_NODE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/UdpMultiCastImplPosix.cpp)
endif()

add_component(
	COMPONENT_NAME	node
	NAMESPACE		lux::communication
	SOURCE_FILES	${LUX_COMMUNICATION_NODE_SRC}
)

component_include_directories(
	node
	BUILD_TIME_EXPORT
		${CMAKE_CURRENT_SOURCE_DIR}/include
		${LUX_GENERATE_HEADER_DIR}
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/pinclude
	INSTALL_TIME
        include
)

target_link_libraries(
	node 
	PUBLIC
	cppzmq
	stduuid
	lux::cxx::concurrent
	lux::cxx::compile_time
)

target_compile_definitions(
	node
	PRIVATE
	LUX_COMMUNICATION_LIBRARY
)

set(ENABLE_NODE_TEST TRUE CACHE BOOL "Enable node test")
if(ENABLE_NODE_TEST)
	add_subdirectory(test)
endif()
