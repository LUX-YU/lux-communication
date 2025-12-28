cmake_minimum_required(VERSION 3.18)
project(lux-communication)
enable_testing()

if(WIN32)
    set(CMAKE_DEBUG_POSTFIX d)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(lux-cmake-toolset CONFIG REQUIRED)

generate_visibility_header(
    ENABLE_MACRO_NAME  LUX_COMMUNICATION_LIBRARY
    PUBLIC_MACRO_NAME  LUX_COMMUNICATION_PUBLIC
    GENERATE_FILE_PATH lux/communication/visibility.h
)

# For debugging purposes, set all output binaries to a common directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

add_subdirectory(protobuf)
add_subdirectory(builtin_msgs)
add_subdirectory(node)

install_components(
    PROJECT_NAME    lux-communication
    VERSION         0.0.1
    NAMESPACE       lux::communication
    COMPONENTS      protobuf
                    builtin_msgs
                    node
)
