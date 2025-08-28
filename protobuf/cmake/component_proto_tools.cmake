# ARGV0		Component
# ARGV1		OutputValue
function(component_get_protos)
	MATH(EXPR NUMBER "${ARGC}-1")
    list(SUBLIST ARGN 1 ${NUMBER} PROTO_LIST)
	component_get_assets(${ARGV0} protos OUTPUT)
	set(${ARGV1} ${OUTPUT} PARENT_SCOPE)
endfunction()

# ARGV0		Component
# ARGV1		OutputValue
function(component_get_proto_files)
	component_get_asset_files(
		${ARGV0}
		protos
		OUTPUT
	)
	set(${ARGV1} ${OUTPUT} PARENT_SCOPE)
endfunction()

# ARGV0		Component
# ARGV1     TYPE
#           BUILD_TIME
#           INSTALL_TIME
# ARGV2...N	Assets		Directories/Files
function(component_proto_include_directories)
    set(_options)
    set(_one_value_arguments)
    set(_multi_value_arguments BUILD_TIME INSTALL_TIME)

    cmake_parse_arguments(
        ARGS
        "${_options}"
        "${_one_value_arguments}"
        "${_multi_value_arguments}"
        ${ARGN}
    )

    if(ARGS_BUILD_TIME)
        foreach(dir ${ARGS_BUILD_TIME})
            if(NOT IS_DIRECTORY ${dir})
                message(FATAL_ERROR "${dir} is not a directories.")
            endif()
            if(NOT IS_ABSOLUTE ${dir})
                message(FATAL_ERROR "${dir} is not a absolute path.")
            endif()
        endforeach()

        component_append_property(${ARGV0} BUILD_TIME_PROTO_INCLUDE_DIRS ${ARGS_BUILD_TIME})
        component_add_assets(${ARGV0} protos ${ARGS_BUILD_TIME})
    endif()

    if(ARGS_INSTALL_TIME)
        foreach(dir ${ARGS_INSTALL_TIME})
            if(IS_ABSOLUTE ${dir})
                message(FATAL_ERROR "Inatall time proto include directory can't be a absolute path.")
            endif()
        endforeach()

        component_add_export_properties(
            ${ARGV0}
            PROPERTIES
            INSTALL_TIME_PROTO_INCLUDE_DIRS ${ARGS_INSTALL_TIME}
        )
    endif()
endfunction()

# -------------------------------------------------------------------
# add_proto_library
#
# Usage (example):
#   add_proto_library(
#       STATIC                              # or SHARED; defaults to STATIC if neither is specified
#       TARGET_NAME         builtin_msgs_protos
#       PROTOS              ${BUILTIN_MSGS_PROTO_FILES}
#       IMPORT_DIRS         ${CMAKE_CURRENT_SOURCE_DIR}/msgs
#       PROTOC_OUT_DIR      ${CMAKE_CURRENT_BINARY_DIR}/protogen
#       EXPORT_MACRO        LUX_COMMUNICATION_PUBLIC
#       INSERT_HEADERS     lux/communication/visibility.h
#       # The following three output variable names have default values and can be omitted:
#       # RESULT_OUTPUT       <TARGET>_RESULT
#       # SOURCE_FILES_OUTPUT <TARGET>_SRCS
#       # HEADER_FILES_OUTPUT <TARGET>_HDRS
#   )
#
# Target features:
# - Generate C++ source/headers: <PROTOC_OUT_DIR>/.../*.pb.cc / *.pb.h
# - Create library target: add_library(<TARGET_NAME> [STATIC|SHARED] ...)
# - Export: target_include_directories(<TARGET_NAME> PUBLIC <PROTOC_OUT_DIR>)
# - Optional: --cpp_out=dllexport_decl=<EXPORT_MACRO>:<OUTDIR>, and insert INSERT_HEADERS in headers
# - Only accepts explicit PROTOS / IMPORT_DIRS
# -------------------------------------------------------------------
function(add_proto_library)
    set(_options STATIC)
    set(_one_value_arguments
        PROTOC_OUT_DIR
        EXPORT_MACRO
        RESULT_OUTPUT
        SOURCE_FILES_OUTPUT
        HEADER_FILES_OUTPUT
        TARGET_NAME
    )
    set(_multi_value_arguments
        PROTOS
        IMPORT_DIRS
        INSERT_HEADERS
    )

    cmake_parse_arguments(
        ARGS
        "${_options}"
        "${_one_value_arguments}"
        "${_multi_value_arguments}"
        ${ARGN}
    )

    # ---- Validate library type ----
    if(ARGS_STATIC AND ARGS_SHARED)
        message(FATAL_ERROR "add_proto_library: STATIC and SHARED cannot be specified simultaneously.")
    endif()
    set(_libtype STATIC)
    if(ARGS_SHARED)
        set(_libtype SHARED)
    endif()

    # ---- Target name ----
    if(NOT ARGS_TARGET_NAME)
        message(FATAL_ERROR "add_proto_library: TARGET_NAME must be specified.")
    endif()
    set(_target "${ARGS_TARGET_NAME}")

    # ---- protoc executable ----
    if(Protobuf_PROTOC_EXECUTABLE)
        set(Protobuf_Compiler ${Protobuf_PROTOC_EXECUTABLE})
    else()
        find_program(Protobuf_Compiler protoc)
    endif()
    if(NOT Protobuf_Compiler)
        message(FATAL_ERROR "protoc does not exist. Please install Protocol Buffers compiler.")
    endif()

    # ---- Output directory default value ----
    if(NOT ARGS_PROTOC_OUT_DIR)
        set(ARGS_PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/protogen)
    endif()
    file(MAKE_DIRECTORY ${ARGS_PROTOC_OUT_DIR})

    # ---- Input files and import directories ----
    if(NOT ARGS_PROTOS)
        message(FATAL_ERROR "add_proto_library: Must provide .proto file list via PROTOS.")
    endif()
    set(PROTO_FILES ${ARGS_PROTOS})
    if(ARGS_IMPORT_DIRS)
        set(IMPORT_DIRECTORIES ${ARGS_IMPORT_DIRS})
    endif()
    list(REMOVE_DUPLICATES PROTO_FILES)
    list(REMOVE_DUPLICATES IMPORT_DIRECTORIES)

    # ---- Basic compilation arguments (excluding input files) ----
    if(ARGS_EXPORT_MACRO)
        set(CPP_OUT_ARG --cpp_out=dllexport_decl=${ARGS_EXPORT_MACRO}:${ARGS_PROTOC_OUT_DIR})
    else()
        set(CPP_OUT_ARG --cpp_out=${ARGS_PROTOC_OUT_DIR})
    endif()

    set(COMPILER_ARGUMENTS ${CPP_OUT_ARG} --experimental_allow_proto3_optional)
    foreach(dir ${IMPORT_DIRECTORIES})
        list(APPEND COMPILER_ARGUMENTS -I=${dir})
    endforeach()

    # ---- Optional: Insert additional includes for header files (at @@protoc_insertion_point(includes)) ----
    set(_need_patch FALSE)
    if(ARGS_INSERT_HEADERS)
        set(_need_patch TRUE)
        set(_patch_script ${CMAKE_CURRENT_BINARY_DIR}/_${_target}_proto_patch_includes.cmake)
        file(WRITE ${_patch_script}
"if(NOT DEFINED HEADER)
  message(FATAL_ERROR \"HEADER not set\")
endif()
if(NOT DEFINED INSERTS)
  message(FATAL_ERROR \"INSERTS not set\")
endif()
file(READ \"\${HEADER}\" _c)
string(REPLACE \"// @@protoc_insertion_point(includes)\" \"// @@protoc_insertion_point(includes)\\n\${INSERTS}\" _c \"\${_c}\")
file(WRITE \"\${HEADER}\" \"\${_c}\")
")
        set(_include_block "")
        foreach(hh ${ARGS_INSERT_HEADERS})
            if(_include_block)
                set(_include_block "${_include_block}\\n#include <${hh}>")
            else()
                set(_include_block "#include <${hh}>")
            endif()
        endforeach()
    endif()

    # ---- Generate rules for each proto ----
    set(ALL_GEN_SRCS)
    set(ALL_GEN_HDRS)

    foreach(proto_file ${PROTO_FILES})
        if(NOT EXISTS ${proto_file})
            message(FATAL_ERROR "Proto file not found: ${proto_file}")
        endif()

        get_filename_component(_proto_abs ${proto_file} ABSOLUTE)
        get_filename_component(_proto_dir ${_proto_abs} DIRECTORY)
        get_filename_component(_proto_name_we ${_proto_abs} NAME_WE)

        # Mirror hierarchy to output directory based on best matching root in IMPORT_DIRS
        set(_rel_dir "")
        set(_best_len -1)
        foreach(root ${IMPORT_DIRECTORIES})
            get_filename_component(_root_abs ${root} ABSOLUTE)
            string(LENGTH "${_root_abs}" _root_len)
            string(FIND "${_proto_dir}/" "${_root_abs}/" _pos)
            if(_pos EQUAL 0 AND _root_len GREATER _best_len)
                file(RELATIVE_PATH _tmp_rel "${_root_abs}" "${_proto_dir}")
                set(_rel_dir "${_tmp_rel}")
                set(_best_len ${_root_len})
            endif()
        endforeach()

        if(_rel_dir STREQUAL "")
            set(_out_dir ${ARGS_PROTOC_OUT_DIR})
        else()
            set(_out_dir ${ARGS_PROTOC_OUT_DIR}/${_rel_dir})
        endif()

        set(_out_cc ${_out_dir}/${_proto_name_we}.pb.cc)
        set(_out_h  ${_out_dir}/${_proto_name_we}.pb.h)
    
        if(_need_patch)
            add_custom_command(
                OUTPUT     ${_out_cc} ${_out_h}
                COMMAND    ${CMAKE_COMMAND} -E make_directory ${_out_dir}
                COMMAND    ${Protobuf_Compiler} ${COMPILER_ARGUMENTS} ${_proto_abs}
                COMMAND    ${CMAKE_COMMAND}
                           -DHEADER=${_out_h}
                           -DINSERTS=${_include_block}
                           -P ${_patch_script}
                DEPENDS    ${_proto_abs}
                COMMENT    "Generating C++ from ${proto_file}"
                VERBATIM
            )
        else()
            add_custom_command(
                OUTPUT     ${_out_cc} ${_out_h}
                COMMAND    ${CMAKE_COMMAND} -E make_directory ${_out_dir}
                COMMAND    ${Protobuf_Compiler} ${COMPILER_ARGUMENTS} ${_proto_abs}
                DEPENDS    ${_proto_abs}
                COMMENT    "Generating C++ from ${proto_file}"
                VERBATIM
            )
        endif()

        list(APPEND ALL_GEN_SRCS ${_out_cc})
        list(APPEND ALL_GEN_HDRS ${_out_h})
    endforeach()

    # ---- Create library target and expose header directories ----
    # Add generated .cc/.h (and patch marker files) as sources to make the build graph complete
    add_library(
        ${_target} 
        ${_libtype}
        ${ALL_GEN_SRCS}
    )
    
    # ---- Return variables to caller (provide default names) ----
    # Default variable names: <TARGET>_RESULT / <TARGET>_SRCS / <TARGET>_HDRS
    string(MAKE_C_IDENTIFIER "${_target}" _tid)
    if(NOT ARGS_RESULT_OUTPUT)
        set(ARGS_RESULT_OUTPUT "${_tid}_RESULT")
    endif()
    if(NOT ARGS_SOURCE_FILES_OUTPUT)
        set(ARGS_SOURCE_FILES_OUTPUT "${_tid}_SRCS")
    endif()
    if(NOT ARGS_HEADER_FILES_OUTPUT)
        set(ARGS_HEADER_FILES_OUTPUT "${_tid}_HDRS")
    endif()

    if(ARGS_RESULT_OUTPUT)
        set(${ARGS_RESULT_OUTPUT} 0 PARENT_SCOPE)
    endif()
    if(ARGS_SOURCE_FILES_OUTPUT)
        list(REMOVE_DUPLICATES ALL_GEN_SRCS)
        set(${ARGS_SOURCE_FILES_OUTPUT} ${ALL_GEN_SRCS} PARENT_SCOPE)
    endif()
    if(ARGS_HEADER_FILES_OUTPUT)
        list(REMOVE_DUPLICATES ALL_GEN_HDRS)
        set(${ARGS_HEADER_FILES_OUTPUT} ${ALL_GEN_HDRS} PARENT_SCOPE)
    endif()

    message(STATUS "Protobuf OUT dir: ${ARGS_PROTOC_OUT_DIR}")
    message(STATUS "Protobuf target:  ${_target} (${_libtype})")
endfunction()
