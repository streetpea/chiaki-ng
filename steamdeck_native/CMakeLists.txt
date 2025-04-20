cmake_minimum_required(VERSION 3.10)

project(sdeck)

option(SDECK_BUILD_DEMOS "Build testing executables for sdeck" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

add_library(sdeck
	include/sdeck.h
	src/sdeck.c
	)

target_include_directories(sdeck PUBLIC include)

if(LINUX)
find_package(HIDAPI REQUIRED COMPONENTS hidraw)
target_link_libraries(sdeck HIDAPI::hidapi-hidraw)
else()
find_package(HIDAPI REQUIRED)
target_link_libraries(sdeck HIDAPI::hidapi)
endif()
if(NOT WIN32)
	target_link_libraries(sdeck m)
endif()
find_package(PkgConfig REQUIRED)
pkg_search_module(FFTW REQUIRED fftw3 IMPORTED_TARGET)
target_link_libraries(sdeck PkgConfig::FFTW)
if(SDECK_BUILD_DEMOS OR CHIAKI_ENABLE_TESTS)
	add_executable(sdeck-demo-motion demo/sdeck_motion.c)
	add_executable(sdeck-demo-haptic demo/sdeck_haptic.c)
	target_link_libraries(sdeck-demo-motion sdeck)
	target_link_libraries(sdeck-demo-haptic sdeck)
endif()

