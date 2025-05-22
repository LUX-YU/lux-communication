find_package(cppzmq   CONFIG REQUIRED)
find_package(stduuid  CONFIG REQUIRED)
find_package(lux-cxx		 REQUIRED COMPONENTS concurrent compile_time)

set(LUX_COMMUNICATION_NODE_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Subscriber.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Executor.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ZmqContext.cpp
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
        zmq
        lux::cxx::concurrent
        lux::cxx::compile_time
        PRIVATE
        stduuid
)

component_add_internal_dependencies(
    node
    lux::communication::builtin_msgs
)

set(USE_SHARED_MESSAGE_MODE FALSE CACHE BOOL "Use shared_ptr as message points")
if(USE_SHARED_MESSAGE_MODE)
	target_compile_definitions(
		node
		PUBLIC
		__USE_SHARED_MESSAGE_MODE__
	)
endif()

set(USE_LOCKFREE_QUEUE TRUE CACHE BOOL "Use cameron314/concurrentqueue")
if(USE_LOCKFREE_QUEUE)
	find_package(concurrentqueue CONFIG)
	if(NOT concurrentqueue_FOUND)
		# Vcpkg
		find_package(unofficial-concurrentqueue CONFIG REQUIRED)
		set(CONCURRENT_QUEUE unofficial::concurrentqueue::concurrentqueue)
		set(CONCURRENT_QUEUE_DEPENDS "find_package(unofficial-concurrentqueue CONFIG REQUIRED)")
	else()
		set(CONCURRENT_QUEUE concurrentqueue::concurrentqueue)
		set(CONCURRENT_QUEUE_DEPENDS "find_package(concurrentqueue CONFIG REQUIRED)")
	endif()
	
	target_link_libraries(
		node
		PUBLIC
		${CONCURRENT_QUEUE}
	)

	target_compile_definitions(
		node
		PUBLIC
		__MACRO_USE_LOCKFREE_QUEUE__
	)

	component_add_transitive_commands(
		node
		"find_package(lux-cxx REQUIRED COMPONENTS concurrent compile_time)"
		"${CONCURRENT_QUEUE_DEPENDS}"
	)
else()
	component_add_transitive_commands(
		node
		"find_package(lux-cxx REQUIRED COMPONENTS concurrent compile_time)"
	)
endif()

target_compile_definitions(
	node
	PRIVATE
	LUX_COMMUNICATION_LIBRARY
)

set(ENABLE_NODE_TEST TRUE CACHE BOOL "Enable node test")
if(ENABLE_NODE_TEST)
	add_subdirectory(test)
endif()
