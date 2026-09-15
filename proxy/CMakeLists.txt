cmake_minimum_required(VERSION 3.16)
project(chiaki-proxy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(chiaki-proxy
	src/main.cpp
	src/posix.cpp
	src/ws.cpp
)

target_include_directories(chiaki-proxy PRIVATE src)

if(WIN32)
	target_link_libraries(chiaki-proxy PRIVATE ws2_32 winhttp crypt32)
	target_compile_definitions(chiaki-proxy PRIVATE _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN NOMINMAX)
	if(MSVC)
		target_compile_options(chiaki-proxy PRIVATE /utf-8 /W3)
	endif()
else()
	if(APPLE AND NOT OPENSSL_ROOT_DIR)
		foreach(p /opt/homebrew/opt/openssl@3 /usr/local/opt/openssl@3 /opt/homebrew/opt/openssl /usr/local/opt/openssl)
			if(EXISTS "${p}/include/openssl/ssl.h")
				set(OPENSSL_ROOT_DIR "${p}")
				break()
			endif()
		endforeach()
	endif()
	find_package(OpenSSL REQUIRED)
	find_package(Threads REQUIRED)
	target_link_libraries(chiaki-proxy PRIVATE OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
	target_compile_options(chiaki-proxy PRIVATE -Wall -Wextra)
endif()

if(APPLE)
	target_compile_definitions(chiaki-proxy PRIVATE CHIAKI_PROXY_APPLE)
endif()

if(WIN32)
	set(PROXY_DIST_NAME "chiaki-proxy-windows.exe")
elseif(APPLE)
	set(PROXY_DIST_NAME "chiaki-proxy-macos")
else()
	set(PROXY_DIST_NAME "chiaki-proxy-linux")
endif()

set(PROXY_WWW_DOWNLOADS "${CMAKE_CURRENT_SOURCE_DIR}/../wasm/www/downloads")
add_custom_command(TARGET chiaki-proxy POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory "${PROXY_WWW_DOWNLOADS}"
	COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:chiaki-proxy>" "${PROXY_WWW_DOWNLOADS}/${PROXY_DIST_NAME}"
	COMMENT "Copy ${PROXY_DIST_NAME} to wasm/www/downloads"
)
