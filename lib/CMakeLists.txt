
set(HEADER_FILES
		include/chiaki/session.h
		include/chiaki/common.h
		include/chiaki/sock.h
		include/chiaki/thread.h
		include/chiaki/base64.h
		include/chiaki/http.h
		include/chiaki/log.h
		include/chiaki/ctrl.h
		include/chiaki/rpcrypt.h
		include/chiaki/takion.h
		include/chiaki/senkusha.h
		include/chiaki/streamconnection.h
		include/chiaki/ecdh.h
		include/chiaki/launchspec.h
		include/chiaki/random.h
		include/chiaki/gkcrypt.h
		include/chiaki/audio.h
		include/chiaki/audioreceiver.h
		include/chiaki/audiosender.h
		include/chiaki/video.h
		include/chiaki/videoreceiver.h
		include/chiaki/frameprocessor.h
		include/chiaki/packetstats.h
		include/chiaki/seqnum.h
		include/chiaki/discovery.h
		include/chiaki/congestioncontrol.h
		include/chiaki/stoppipe.h
		include/chiaki/reorderqueue.h
		include/chiaki/discoveryservice.h
		include/chiaki/feedback.h
		include/chiaki/feedbacksender.h
		include/chiaki/controller.h
		include/chiaki/takionsendbuffer.h
		include/chiaki/time.h
		include/chiaki/fec.h
		include/chiaki/regist.h
		include/chiaki/opusdecoder.h
		include/chiaki/opusencoder.h
		include/chiaki/orientation.h
		include/chiaki/bitstream.h)

set(SOURCE_FILES
		src/common.c
		src/sock.c
		src/session.c
		src/thread.c
		src/base64.c
		src/http.c
		src/log.c
		src/ctrl.c
		src/rpcrypt.c
		src/takion.c
		src/senkusha.c
		src/utils.h
		src/pb_utils.h
		src/streamconnection.c
		src/ecdh.c
		src/launchspec.c
		src/random.c
		src/gkcrypt.c
		src/audio.c
		src/audioreceiver.c
		src/audiosender.c
		src/videoreceiver.c
		src/frameprocessor.c
		src/packetstats.c
		src/discovery.c
		src/congestioncontrol.c
		src/stoppipe.c
		src/reorderqueue.c
		src/discoveryservice.c
		src/feedback.c
		src/feedbacksender.c
		src/controller.c
		src/takionsendbuffer.c
		src/time.c
		src/fec.c
		src/regist.c
		src/opusdecoder.c
		src/opusencoder.c
		src/orientation.c
		src/bitstream.c)

# RUDP/holepunch files require miniupnpc, only include if enabled
if(CHIAKI_ENABLE_RUDP)
	list(APPEND HEADER_FILES
		include/chiaki/remote/holepunch.h
		include/chiaki/remote/rudp.h
		include/chiaki/remote/rudpsendbuffer.h)
	list(APPEND SOURCE_FILES
		src/remote/holepunch.c
		src/remote/rudp.c
		src/remote/rudpsendbuffer.c)
endif()

if(CHIAKI_ENABLE_FFMPEG_DECODER)
	list(APPEND HEADER_FILES include/chiaki/ffmpegdecoder.h)
	list(APPEND SOURCE_FILES src/ffmpegdecoder.c)
endif()
set(CHIAKI_LIB_ENABLE_PI_DECODER "${CHIAKI_ENABLE_FFMPEG_DECODER}")

if(CHIAKI_ENABLE_PI_DECODER)
	list(APPEND HEADER_FILES include/chiaki/pidecoder.h)
	list(APPEND SOURCE_FILES src/pidecoder.c)
endif()
set(CHIAKI_LIB_ENABLE_PI_DECODER "${CHIAKI_ENABLE_PI_DECODER}")

add_subdirectory(protobuf)
set_source_files_properties(${CHIAKI_LIB_PROTO_SOURCE_FILES} ${CHIAKI_LIB_PROTO_HEADER_FILES} PROPERTIES GENERATED TRUE)
include_directories("${CHIAKI_LIB_PROTO_INCLUDE_DIR}")

if(CHIAKI_LIB_ENABLE_OPUS)
	find_package(Opus REQUIRED)
	include_directories(${Opus_INCLUDE_DIRS})
endif()

add_library(chiaki-lib ${HEADER_FILES} ${SOURCE_FILES} ${CHIAKI_LIB_PROTO_SOURCE_FILES} ${CHIAKI_LIB_PROTO_HEADER_FILES})
configure_file(config.h.in include/chiaki/config.h)
target_include_directories(chiaki-lib PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/include")

if(CMAKE_C_COMPILER_ID STREQUAL GNU OR CMAKE_C_COMPILER_ID STREQUAL Clang)
	target_compile_options(chiaki-lib PRIVATE -Wall)
endif()

add_dependencies(chiaki-lib chiaki-pb)
set_target_properties(chiaki-lib PROPERTIES OUTPUT_NAME chiaki)

if(WIN32)
	target_link_libraries(chiaki-lib wsock32 ws2_32 bcrypt iphlpapi)
endif()

if(APPLE)
	target_link_libraries(chiaki-lib "-framework CoreServices")
endif()

target_include_directories(chiaki-lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

find_package(Threads REQUIRED)
target_link_libraries(chiaki-lib Threads::Threads)

if (CHIAKI_IS_SWITCH)
	find_library(JSONC_LIB json-c ${PORTLIBS}/lib)
	target_link_libraries(chiaki-lib ${JSONC_LIB})
	target_include_directories(chiaki-lib PUBLIC "${PORTLIBS}/include/json-c")
elseif(ANDROID)
	# Use FetchContent for Android (no pkg-config)
	include(FetchContent)
	
	# json-c
	FetchContent_Declare(
		json-c
		GIT_REPOSITORY https://github.com/json-c/json-c.git
		GIT_TAG json-c-0.16-20220414
	)
	set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
	set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
	set(DISABLE_WERROR ON CACHE BOOL "" FORCE)
	FetchContent_GetProperties(json-c)
	if(NOT json-c_POPULATED)
		FetchContent_Populate(json-c)
		# Patch json-c to disable apps and tests
		file(READ "${json-c_SOURCE_DIR}/CMakeLists.txt" JSONC_CMAKE)
		string(REPLACE "add_subdirectory(apps)" "# add_subdirectory(apps)" JSONC_CMAKE "${JSONC_CMAKE}")
		string(REPLACE "add_subdirectory(tests)" "# add_subdirectory(tests)" JSONC_CMAKE "${JSONC_CMAKE}")
		file(WRITE "${json-c_SOURCE_DIR}/CMakeLists.txt" "${JSONC_CMAKE}")
		add_subdirectory(${json-c_SOURCE_DIR} ${json-c_BINARY_DIR})
	endif()
	target_link_libraries(chiaki-lib json-c)
	target_include_directories(chiaki-lib PUBLIC ${json-c_SOURCE_DIR} ${json-c_BINARY_DIR})
	# Create symlink for #include <json-c/json_object.h>
	file(MAKE_DIRECTORY ${json-c_BINARY_DIR}/json-c)
	file(GLOB JSONC_HEADERS "${json-c_SOURCE_DIR}/*.h")
	foreach(header ${JSONC_HEADERS})
		get_filename_component(header_name ${header} NAME)
		file(CREATE_LINK ${header} ${json-c_BINARY_DIR}/json-c/${header_name} COPY_ON_ERROR)
	endforeach()
	# Also copy generated headers
	file(GLOB JSONC_GEN_HEADERS "${json-c_BINARY_DIR}/*.h")
	foreach(header ${JSONC_GEN_HEADERS})
		get_filename_component(header_name ${header} NAME)
		if(NOT EXISTS ${json-c_BINARY_DIR}/json-c/${header_name})
			file(CREATE_LINK ${header} ${json-c_BINARY_DIR}/json-c/${header_name} COPY_ON_ERROR)
		endif()
	endforeach()
else()
	find_package(PkgConfig REQUIRED)
	pkg_search_module(json-c REQUIRED json-c IMPORTED_TARGET)
	target_include_directories(chiaki-lib PRIVATE ${json-c_INCLUDE_DIRS})
	target_link_libraries(chiaki-lib PkgConfig::json-c)
endif()

# miniupnpc - only needed if RUDP is enabled
if(CHIAKI_ENABLE_RUDP)
	if(ANDROID)
		# Use FetchContent for Android
		FetchContent_Declare(
			miniupnpc
			GIT_REPOSITORY https://github.com/miniupnp/miniupnp.git
			GIT_TAG miniupnpc_2_2_5
			SOURCE_SUBDIR miniupnpc
		)
		set(UPNPC_BUILD_STATIC ON CACHE BOOL "" FORCE)
		set(UPNPC_BUILD_SHARED OFF CACHE BOOL "" FORCE)
		set(UPNPC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
		set(UPNPC_BUILD_SAMPLE OFF CACHE BOOL "" FORCE)
		FetchContent_MakeAvailable(miniupnpc)
		target_link_libraries(chiaki-lib libminiupnpc-static)
		# Add include directory for miniupnpc (holepunch.c uses #include <miniupnpc/miniupnpc.h>)
		# miniupnpc headers are in _SOURCE_DIR/miniupnpc/include/ but we need miniupnpc/ subdir
		file(MAKE_DIRECTORY ${miniupnpc_SOURCE_DIR}/miniupnpc/include/miniupnpc)
		file(GLOB MINIUPNPC_HEADERS "${miniupnpc_SOURCE_DIR}/miniupnpc/include/*.h")
		foreach(header ${MINIUPNPC_HEADERS})
			get_filename_component(header_name ${header} NAME)
			file(CREATE_LINK ${header} ${miniupnpc_SOURCE_DIR}/miniupnpc/include/miniupnpc/${header_name} COPY_ON_ERROR)
		endforeach()
		target_include_directories(chiaki-lib PRIVATE ${miniupnpc_SOURCE_DIR}/miniupnpc/include)
	else()
		find_package(PkgConfig REQUIRED)
		pkg_search_module(MINIUPNPC REQUIRED miniupnpc IMPORTED_TARGET)
		target_link_libraries(chiaki-lib PkgConfig::MINIUPNPC)
	endif()
endif()

target_link_libraries(chiaki-lib CURL::libcurl)

if(CHIAKI_LIB_ENABLE_MBEDTLS)
	if(CHIAKI_LIB_MBEDTLS_EXTERNAL_PROJECT)
		target_link_libraries(chiaki-lib mbedtls mbedx509 mbedcrypto)
	else()
		# provided by mbedtls-static (mbedtls-devel)
		find_library(MBEDTLS mbedtls)
		find_library(MBEDX509 mbedx509)
		find_library(MBEDCRYPTO mbedcrypto)
		target_link_libraries(chiaki-lib ${MBEDTLS} ${MBEDX509} ${MBEDCRYPTO})
	endif()
elseif(CHIAKI_LIB_OPENSSL_EXTERNAL_PROJECT)
	target_link_libraries(chiaki-lib OpenSSL_Crypto)
else()
	# default
	find_package(OpenSSL REQUIRED)
	target_link_libraries(chiaki-lib OpenSSL::Crypto)
endif()

target_link_libraries(chiaki-lib Nanopb::nanopb)
target_link_libraries(chiaki-lib Jerasure::Jerasure)

if(CHIAKI_ENABLE_FFMPEG_DECODER)
	target_link_libraries(chiaki-lib FFMPEG::avcodec FFMPEG::avutil)
endif()

if(CHIAKI_ENABLE_PI_DECODER)
	target_link_libraries(chiaki-lib ILClient::ILClient)
endif()

if(CHIAKI_LIB_ENABLE_OPUS)
	target_link_libraries(chiaki-lib ${Opus_LIBRARIES})
endif()

#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fanalyzer")
if(CHIAKI_ENABLE_TESTS)
	add_executable(holepunch-test include/chiaki/remote/holepunch.h src/remote/holepunch-test.c)
	target_link_libraries(holepunch-test chiaki-lib)
endif()