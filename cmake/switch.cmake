
# Find DEVKITPRO
set(DEVKITPRO "$ENV{DEVKITPRO}" CACHE PATH "Path to DevKitPro")
if(NOT DEVKITPRO)
	message(FATAL_ERROR "Please set DEVKITPRO env before calling cmake. https://devkitpro.org/wiki/Getting_Started")
endif()

# include devkitpro toolchain
include("${DEVKITPRO}/cmake/Switch.cmake")

set(NSWITCH TRUE)

# troubleshoot
message(STATUS "CMAKE_FIND_ROOT_PATH = ${CMAKE_FIND_ROOT_PATH}")
message(STATUS "PKG_CONFIG_EXECUTABLE = ${PKG_CONFIG_EXECUTABLE}")
message(STATUS "CMAKE_EXE_LINKER_FLAGS = ${CMAKE_EXE_LINKER_FLAGS}")
get_property(include_directories DIRECTORY PROPERTY INCLUDE_DIRECTORIES)
message(STATUS "INCLUDE_DIRECTORIES = ${include_directories}")
message(STATUS "CMAKE_C_COMPILER = ${CMAKE_C_COMPILER}")
message(STATUS "CMAKE_CXX_COMPILER = ${CMAKE_CXX_COMPILER}")

find_program(ELF2NRO elf2nro ${DEVKITPRO}/tools/bin)
if (ELF2NRO)
	message(STATUS "elf2nro: ${ELF2NRO} - found")
else()
	message(WARNING "elf2nro - not found")
endif()

find_program(NACPTOOL nacptool ${DEVKITPRO}/tools/bin)
if (NACPTOOL)
	message(STATUS "nacptool: ${NACPTOOL} - found")
else()
	message(WARNING "nacptool - not found")
endif()

function(__add_nacp target APP_TITLE APP_AUTHOR APP_VERSION)
	set(__NACP_COMMAND ${NACPTOOL} --create ${APP_TITLE} ${APP_AUTHOR} ${APP_VERSION} ${CMAKE_CURRENT_BINARY_DIR}/${target})

	add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${target}
		COMMAND ${__NACP_COMMAND}
		WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
		VERBATIM
		)
endfunction()

function(add_nro_target output_name target title author version icon romfs)
	if(NOT ${output_name}.nacp)
		__add_nacp(${output_name}.nacp ${title} ${author} ${version})
	endif()
	add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${output_name}.nro
		COMMAND ${ELF2NRO} $<TARGET_FILE:${target}>
		${CMAKE_CURRENT_BINARY_DIR}/${output_name}.nro
		--icon=${icon}
		--nacp=${CMAKE_CURRENT_BINARY_DIR}/${output_name}.nacp
		--romfsdir=${romfs}
		DEPENDS ${target} ${CMAKE_CURRENT_BINARY_DIR}/${output_name}.nacp
		VERBATIM
		)
	add_custom_target(${output_name}_nro ALL SOURCES ${CMAKE_CURRENT_BINARY_DIR}/${output_name}.nro)
endfunction()

set(CMAKE_USE_SYSTEM_ENVIRONMENT_PATH OFF)
