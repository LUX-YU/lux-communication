# INCLUDE GUARD
if(_COMPONENT_PROTO_TOOLS_INCLUDED_)
	return()
endif()
set(_COMPONENT_PROTO_TOOLS_INCLUDED_ TRUE)

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

# KEY1				COMPONENTS		<components...>
# KEY2				PROTOS			<protobuf files...>
# KEY3				IMPORT_DIRS		<paths...>
# KEY4				EXPORT_MACRO
# KEY5				PROTOC_OUT_DIR
function(components_generate_protos)
	set(_options)
	set(_one_value_arguments
		PROTOC_OUT_DIR
		EXPORT_MACRO
		RESULT_OUTPUT
		SOURCE_FILES_OUTPUT
		HEADER_FILES_OUTPUT
	)
	set(_multi_value_arguments	
		COMPONENTS	
		PROTOS	
		IMPORT_DIRS 
		INCLUDE_HEADERS
	)

	cmake_parse_arguments(
		ARGS
		"${_options}"
		"${_one_value_arguments}"
		"${_multi_value_arguments}"
		${ARGN}
	)

	if(NOT ARGS_PROTOC_OUT_DIR)
		set(ARGS_PROTOC_OUT_DIR ${CMAKE_BINARY_DIR}/protogen)
	endif()
	if(NOT EXISTS ${ARGS_PROTOC_OUT_DIR})
		file(MAKE_DIRECTORY ${ARGS_PROTOC_OUT_DIR})
	endif()

	set(PROTO_FILES)
	set(IMPORT_DIRECTORIES ${ARGS_IMPORT_DIRS})
	if(ARGS_COMPONENTS)
		foreach(component ${ARGS_COMPONENTS})
			get_target_property(IS_IMPORTED ${component} IMPORTED_COMPONENT)
			if(NOT IS_IMPORTED)
                get_target_property(PROTO_INCLUDE_DIRS ${component} BUILD_TIME_PROTO_INCLUDE_DIRS)
				list(APPEND IMPORT_DIRECTORIES ${PROTO_INCLUDE_DIRS})
			else()
                get_target_property(COMPONENT_ASSET_PREFIX ${component} IMPORTED_ASSETS_PREFIX)
                get_target_property(INSTALL_TIME_PROTO_DIRS ${component} INSTALL_TIME_PROTO_INCLUDE_DIRS)
                set(TYPE_PROTO_PREFIX ${COMPONENT_ASSET_PREFIX}/protos)
                foreach(dir ${INSTALL_TIME_PROTO_DIRS})
                    list(APPEND IMPORT_DIRECTORIES ${TYPE_PROTO_PREFIX}/${dir})
                endforeach()
            endif()

			component_get_proto_files(${component} COMPONENT_PROTO_FILES)
			list(APPEND PROTO_FILES ${COMPONENT_PROTO_FILES})
		endforeach()
	endif()

	if(ARGS_PROTOS)
		list(PROTO_FILES APPEND ${ARGS_PROTOS})
	endif()

	if(NOT EXISTS ${ARGS_PROTOC_OUT_DIR})
		file(MAKE_DIRECTORY ${ARGS_PROTOC_OUT_DIR})
	endif()

	if(ARGS_EXPORT_MACRO)
		set(COMPILER_ARGUMENTS --cpp_out=dllexport_decl=${ARGS_EXPORT_MACRO}:${ARGS_PROTOC_OUT_DIR})
	else()
		set(COMPILER_ARGUMENTS --cpp_out=${ARGS_PROTOC_OUT_DIR})
	endif()
	foreach(dir ${IMPORT_DIRECTORIES})
		list(APPEND COMPILER_ARGUMENTS -I=${dir})
	endforeach()

	foreach(proto_file ${PROTO_FILES})
		list(APPEND COMPILER_ARGUMENTS ${proto_file})
	endforeach()

	execute_process(
		COMMAND				${Protobuf_PROTOC_EXECUTABLE} ${COMPILER_ARGUMENTS}
		RESULT_VARIABLE		OUTPUT
	)

	if(ARGS_RESULT_OUTPUT)
		set(${ARGS_RESULT_OUTPUT} ${OUTPUT} PARENT_SCOPE)
	endif()

	if(ARGS_SOURCE_FILES_OUTPUT)
		file(GLOB_RECURSE GENERATED_SOURCE_FILES 
			LIST_DIRECTORIES false 
			"${ARGS_PROTOC_OUT_DIR}/*.cc"
		)

		list(REMOVE_DUPLICATES GENERATED_SOURCE_FILES)
		set(${ARGS_SOURCE_FILES_OUTPUT} ${GENERATED_SOURCE_FILES} PARENT_SCOPE)
	endif()

	if(ARGS_INCLUDE_HEADERS)
		# build command
		set(INCLUDE_COMMAND "// @@protoc_insertion_point(includes)")
		foreach(header ${ARGS_INCLUDE_HEADERS})
			set(INCLUDE_COMMAND "${INCLUDE_COMMAND}\n#include <${header}>")
		endforeach()

		# find header files
		file(GLOB_RECURSE GENERATED_HEADER_FILES 
			LIST_DIRECTORIES false 
			"${ARGS_PROTOC_OUT_DIR}/*.h"
		)
		list(REMOVE_DUPLICATES GENERATED_HEADER_FILES)

		# replace string
		foreach(header ${GENERATED_HEADER_FILES})
			file(READ ${header} FILE_CONTENTS)
			string(REPLACE "// @@protoc_insertion_point(includes)" "${INCLUDE_COMMAND}" FILE_CONTENTS "${FILE_CONTENTS}")
			file(WRITE ${header} "${FILE_CONTENTS}")
		endforeach()
		
		if(ARGS_HEADER_FILES_OUTPUT)
			set(${ARGS_HEADER_FILES_OUTPUT} ${GENERATED_HEADER_FILES} PARENT_SCOPE)
		endif()
	endif()
endfunction()
