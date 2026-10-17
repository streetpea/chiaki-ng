# SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

cmake_minimum_required(VERSION 3.13)

project(chiaki-wiiu C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

add_executable(chiaki-wiiu
	src/main.cpp
	src/video.cpp
	src/config.cpp
	src/gui.cpp
	# Fase 3b: audio de sesion por la voz AX (snd_core2).
	src/audio.cpp
)

# Fase 3a: h264.rpl (H264DEC*) esta resuelto dentro de libwut.a via
# wut.specs; no hace falta libreria adicional (-lwut implicito).

# libwut.a (via -lwut automatico del toolchain) ya contiene VPAD, WHB, GX2 y
# nsysnet como imports RPL resueltos por wut.specs; no existen libs separadas
# -lvpad/-lwhb/-lgx2/-lnsysnet.
target_link_libraries(chiaki-wiiu
	chiaki-lib
)
# Nota: curl/json-c ya no se usan en el frontend (la cuenta PSN se introduce
# a mano desde la fase 2b); chiaki-lib los arrastra igualmente si alguna
# ruta los necesita (p. ej. holepunch de PS5).

# Los headers publicos de chiaki (ecdh.h, rpcrypt.h...) incluyen mbedtls/...
# porque el backend es CHIAKI_LIB_ENABLE_MBEDTLS; portlibs no esta en el
# include path global del toolchain (mismo find_path que usa lib/).
find_path(MBEDTLS_INCLUDE_DIRS mbedtls/ssl.h PATH_SUFFIXES include)
target_include_directories(chiaki-wiiu PRIVATE ${MBEDTLS_INCLUDE_DIRS})

# Mismo shim IPv4-only que lib/ (lib/CMakeLists.txt): wut no declara
# INET6_ADDRSTRLEN ni sockaddr_in6, pero headers publicos de chiaki
# (remote/holepunch.h) los referencian.
target_compile_options(chiaki-wiiu PRIVATE
	-include "${CMAKE_SOURCE_DIR}/lib/src/wiiu-ipv6-shim.h")

wut_create_rpx(chiaki-wiiu)

wut_create_wuhb(chiaki-wiiu
	NAME "Chiaki NG"
	SHORTNAME "Chiaki"
	AUTHOR "chiaki-ng contributors"
	# Icono real de la app en el menu de la Wii U (Aroma/HBL): el
	# wordmark + botones de chiaki-ng sobre el gris Material #121212.
	ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.png"
)
