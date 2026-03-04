find_package(cppzmq   CONFIG QUIET)  # Optional: ZMQ replaced by SHM Transport in Phase 2
find_package(stduuid  CONFIG REQUIRED)
find_package(lux-cxx		 REQUIRED COMPONENTS concurrent compile_time algorithm)

set(LUX_COMMUNICATION_NODE_SRC
	${CMAKE_CURRENT_SOURCE_DIR}/src/Domain.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/TopicBase.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/CallbackGroupBase.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/ExecutorBase.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/executor/SingleThreadedExecutor.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/executor/MultiThreadedExecutor.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/executor/TimeOrderedExecutor.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/executor/SeqOrderedExecutor.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/NodeBase.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/PublisherBase.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/SubscriberBase.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/TimeExecEntry.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/discovery/ShmRegistry.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/discovery/MulticastAnnouncer.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/discovery/DiscoveryService.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/transport/ShmRingWriter.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/transport/ShmRingReader.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/transport/ShmDataPool.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/transport/FragmentSender.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/transport/FragmentAssembler.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/transport/UdpTransportWriter.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/transport/UdpTransportReader.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/transport/TcpTransportWriter.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/transport/TcpTransportReader.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/IoThread.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/unified/Node.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/TokenBucket.cpp
)

if(WIN32)
	LIST(APPEND LUX_COMMUNICATION_NODE_SRC
		${CMAKE_CURRENT_SOURCE_DIR}/src/interprocess/UdpMultiCastImplWin.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/src/platform/PlatformDefsWin.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/src/platform/SharedMemoryWin.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/src/transport/ShmNotifyWin.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/src/platform/NetSocketWin.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/src/transport/IoReactorWin.cpp
	)
else()
	LIST(APPEND LUX_COMMUNICATION_NODE_SRC
		${CMAKE_CURRENT_SOURCE_DIR}/src/interprocess/UdpMultiCastImplPosix.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/src/platform/PlatformDefsPosix.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/src/platform/SharedMemoryPosix.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/src/transport/ShmNotifyPosix.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/src/platform/NetSocketPosix.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/src/transport/IoReactorPosix.cpp
	)
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
        lux::cxx::concurrent
        lux::cxx::compile_time
        lux::cxx::algorithm
        PRIVATE
        stduuid
)

if(cppzmq_FOUND)
    target_link_libraries(node PUBLIC cppzmq)
    target_compile_definitions(node PUBLIC LUX_HAS_ZMQ)
endif()

if(WIN32)
	target_link_libraries(node PRIVATE ws2_32)
else()
	target_link_libraries(node PRIVATE rt)
endif()

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
	find_package(concurrentqueue CONFIG QUIET)
	if(NOT concurrentqueue_FOUND)
		# Vcpkg
		find_package(unofficial-concurrentqueue CONFIG REQUIRED)
		set(CONCURRENT_QUEUE unofficial::concurrentqueue::concurrentqueue)
		set(CONCURRENT_QUEUE_DEPENDS "find_package(unofficial-concurrentqueue CONFIG REQUIRED)")
		message(STATUS "Using cameron314/concurrentqueue")
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
