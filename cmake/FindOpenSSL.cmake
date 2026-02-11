# Custom FindOpenSSL module for chiaki-ng
#
# When OpenSSL is built as a CMake ExternalProject (CHIAKI_LIB_OPENSSL_EXTERNAL_PROJECT),
# the standard FindOpenSSL.cmake cannot find the libraries because they don't exist yet
# at configure time. In that case, OpenSSLExternalProject.cmake has already created the
# OpenSSL::Crypto and OpenSSL::SSL targets, so we just report success.
#
# For all other cases, delegate to the system FindOpenSSL.cmake.

if(CHIAKI_LIB_OPENSSL_EXTERNAL_PROJECT AND TARGET OpenSSL::Crypto)
	# Targets already provided by OpenSSLExternalProject.cmake
	set(OPENSSL_FOUND TRUE)
	set(OpenSSL_FOUND TRUE)
	return()
endif()

# Fall through to the standard CMake FindOpenSSL module
include(${CMAKE_ROOT}/Modules/FindOpenSSL.cmake)
