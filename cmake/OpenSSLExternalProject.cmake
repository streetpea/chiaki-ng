
include(ExternalProject)

set(OPENSSL_INSTALL_DIR "${CMAKE_CURRENT_BINARY_DIR}/openssl-install-prefix")

unset(OPENSSL_OS_COMPILER)
unset(OPENSSL_CONFIG_EXTRA_ARGS)
unset(OPENSSL_BUILD_ENV)

if(ANDROID_ABI)
	if(ANDROID_ABI STREQUAL "armeabi-v7a")
		set(OPENSSL_OS_COMPILER "android-arm")
	elseif(ANDROID_ABI STREQUAL "arm64-v8a")
		set(OPENSSL_OS_COMPILER "android-arm64")
	elseif(ANDROID_ABI STREQUAL "x86")
		set(OPENSSL_OS_COMPILER "android-x86")
	elseif(ANDROID_ABI STREQUAL "x86_64")
		set(OPENSSL_OS_COMPILER "android-x86_64")
	endif()

	set(OPENSSL_CONFIG_EXTRA_ARGS "-D__ANDROID_API__=${ANDROID_NATIVE_API_LEVEL}")
	get_filename_component(ANDROID_NDK_BIN_PATH "${CMAKE_C_COMPILER}" DIRECTORY)
	set(OPENSSL_BUILD_ENV "ANDROID_NDK_HOME=${ANDROID_NDK}" "PATH=${ANDROID_NDK_BIN_PATH}:$ENV{PATH}")
else()
	if(UNIX AND NOT APPLE AND CMAKE_SIZEOF_VOID_P STREQUAL "8")
		set(OPENSSL_OS_COMPILER "linux-x86_64")
	endif()
endif()

if(NOT OPENSSL_OS_COMPILER)
	message(FATAL_ERROR "Failed to match OPENSSL_OS_COMPILER")
endif()

find_program(MAKE_EXE NAMES gmake make)
ExternalProject_Add(OpenSSL-ExternalProject
		URL https://www.openssl.org/source/openssl-1.1.1w.tar.gz
		URL_HASH SHA256=cf3098950cb4d853ad95c0841f1f9c6d3dc102dccfcacd521d93925208b76ac8
		INSTALL_DIR "${OPENSSL_INSTALL_DIR}"
		CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env ${OPENSSL_BUILD_ENV}
			"<SOURCE_DIR>/Configure" "--prefix=<INSTALL_DIR>" no-shared ${OPENSSL_CONFIG_EXTRA_ARGS} "${OPENSSL_OS_COMPILER}"
		BUILD_COMMAND ${CMAKE_COMMAND} -E env ${OPENSSL_BUILD_ENV} "${MAKE_EXE}" -j4 build_libs
		INSTALL_COMMAND ${CMAKE_COMMAND} -E env ${OPENSSL_BUILD_ENV} "${MAKE_EXE}" install_dev)

add_library(OpenSSL_Crypto INTERFACE)
add_dependencies(OpenSSL_Crypto OpenSSL-ExternalProject)
if(${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.13.0")
	target_link_directories(OpenSSL_Crypto INTERFACE "${OPENSSL_INSTALL_DIR}/lib")
else()
	link_directories("${OPENSSL_INSTALL_DIR}/lib")
endif()
target_link_libraries(OpenSSL_Crypto INTERFACE crypto ssl)
target_include_directories(OpenSSL_Crypto INTERFACE "${OPENSSL_INSTALL_DIR}/include")

# Create standard OpenSSL::* IMPORTED INTERFACE targets for find_package(OpenSSL) consumers
# (e.g. bundled curl). Using IMPORTED INTERFACE avoids file-existence checks that IMPORTED
# STATIC would require, and IMPORTED targets are excluded from curl's export sets.
file(MAKE_DIRECTORY "${OPENSSL_INSTALL_DIR}/include")

if(NOT TARGET OpenSSL::Crypto)
	add_library(OpenSSL::Crypto INTERFACE IMPORTED GLOBAL)
	set_property(TARGET OpenSSL::Crypto PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INSTALL_DIR}/include")
	set_property(TARGET OpenSSL::Crypto PROPERTY INTERFACE_LINK_DIRECTORIES "${OPENSSL_INSTALL_DIR}/lib")
	set_property(TARGET OpenSSL::Crypto PROPERTY INTERFACE_LINK_LIBRARIES crypto)
	add_dependencies(OpenSSL::Crypto OpenSSL-ExternalProject)
endif()

if(NOT TARGET OpenSSL::SSL)
	add_library(OpenSSL::SSL INTERFACE IMPORTED GLOBAL)
	set_property(TARGET OpenSSL::SSL PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INSTALL_DIR}/include")
	set_property(TARGET OpenSSL::SSL PROPERTY INTERFACE_LINK_DIRECTORIES "${OPENSSL_INSTALL_DIR}/lib")
	set_property(TARGET OpenSSL::SSL PROPERTY INTERFACE_LINK_LIBRARIES "ssl;OpenSSL::Crypto")
	add_dependencies(OpenSSL::SSL OpenSSL-ExternalProject)
endif()

# Mark OpenSSL as found so FindOpenSSL.cmake is a no-op
set(OPENSSL_FOUND TRUE CACHE BOOL "" FORCE)
set(OpenSSL_FOUND TRUE CACHE BOOL "" FORCE)
set(OPENSSL_VERSION "1.1.1w" CACHE STRING "" FORCE)
set(OPENSSL_INCLUDE_DIR "${OPENSSL_INSTALL_DIR}/include" CACHE PATH "" FORCE)
set(OPENSSL_CRYPTO_LIBRARY "${OPENSSL_INSTALL_DIR}/lib/libcrypto.a" CACHE FILEPATH "" FORCE)
set(OPENSSL_SSL_LIBRARY "${OPENSSL_INSTALL_DIR}/lib/libssl.a" CACHE FILEPATH "" FORCE)
