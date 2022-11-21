cmake_minimum_required(VERSION 3.2)

project(sdeck)

option(SDECK_BUILD_DEMOS "Build testing executables for sdeck" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

add_library(sdeck
	include/sdeck.h
	src/sdeck.c
	)

target_include_directories(sdeck PUBLIC include)

find_package(HIDAPI REQUIRED)
target_link_libraries(sdeck HIDAPI::hidapi)
target_link_libraries(sdeck m)
target_link_libraries(sdeck fftw3)
if(SDECK_BUILD_DEMOS)
	add_executable(sdeck-test src/sdeck_test.c)
	target_link_libraries(sdeck-test sdeck)
endif()

