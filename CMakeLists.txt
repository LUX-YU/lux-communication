cmake_minimum_required(VERSION 3.18)
project(lux-communication)

if(WIN32)
    set(CMAKE_DEBUG_POSTFIX d)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(lux-cmake-toolset CONFIG REQUIRED)

generate_visibility_header(
    ENABLE_MACRO_NAME               LUX_COMMUNICATION_LIBRARY
    PUBLIC_MACRO_NAME               LUX_COMMUNICATION_PUBLIC
    GENERATE_FILE_PATH              lux/communication/visibility.h
)

add_subdirectory(protobuf)
add_subdirectory(builtin_msgs)

install_components(
    PROJECT_NAME    lux-communication
    VERSION         0.0.1
    NAMESPACE       lux::communication
    COMPONENTS      protobuf
                    builtin_msgs
)
