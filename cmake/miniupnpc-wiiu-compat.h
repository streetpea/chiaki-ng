// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

/*
 * Shim de compatibilidad para compilar miniupnpc 2.2.8 contra wut (Wii U).
 *
 * nsysnet (Cafe OS) es IPv4-only: wut no declara sockaddr_in6/AF_INET6 ni
 * SO_RCVTIMEO/SO_SNDTIMEO. miniupnpc usa IPv6 solo en ramas que en runtime
 * nunca se ejecutan con direcciones IPv4 (chiaki solo pasa AF_INET), así que
 * basta con proveer los tipos mínimos para que compile.
 */

#pragma once

#include <sys/socket.h>
#include <netinet/in.h>
#include <stdint.h>
#include <string.h>

/* wut no trae <net/if.h> ni <sys/un.h>: se proveen como cabeceras stub reales
 * en cmake/miniupnpc-stub-include/ (añadido al -I por MiniupnpcExternalProject
 * en Wii U), porque un #include aborta antes de evaluar cualquier guard. */

#ifndef AF_INET6
#define AF_INET6 24
#endif

#ifndef IPPROTO_IPV6
#define IPPROTO_IPV6 41
#endif

/* Estándar BSD; nsysnet no los declara y su uso falla de forma benigna
 * (solo imprime un error y continúa). */
#ifndef SO_RCVTIMEO
#define SO_RCVTIMEO 0x1006
#endif
#ifndef SO_SNDTIMEO
#define SO_SNDTIMEO 0x1005
#endif

#ifndef IPV6_UNICAST_HOPS
#define IPV6_UNICAST_HOPS 16
#endif
#ifndef IPV6_MULTICAST_IF
#define IPV6_MULTICAST_IF 17
#endif
#ifndef IPV6_MULTICAST_HOPS
#define IPV6_MULTICAST_HOPS 18
#endif
#ifndef IPV6_MULTICAST_LOOP
#define IPV6_MULTICAST_LOOP 19
#endif

struct in6_addr {
	uint8_t s6_addr[16];
};

struct sockaddr_in6 {
	uint16_t sin6_family;
	uint16_t sin6_port;
	uint32_t sin6_flowinfo;
	struct in6_addr sin6_addr;
	uint32_t sin6_scope_id;
};

/* libwut no exporta estos símbolos; con valores cero bastan (solo se usan en
 * ramas IPv6 que no se ejecutan). */
static const struct in6_addr in6addr_any;
static const struct in6_addr in6addr_loopback;
