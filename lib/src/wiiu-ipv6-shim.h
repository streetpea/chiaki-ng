// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

/*
 * Shim IPv4-only para compilar chiaki-lib contra wut (Wii U).
 *
 * nsysnet (Cafe OS) es IPv4-only: wut no declara sockaddr_in6, AF_INET6,
 * INET6_ADDRSTRLEN, ni trae <netinet/ip.h>. La lógica de la lib usada en Wii
 * U (takion/ctrl/senkusha/holepunch) es IPv4; las referencias a IPv6 viven en
 * helpers inline (utils.h) o en structs (holepunch.h) que nunca se usan con
 * familias IPv6 en runtime. Aquí se provee lo mínimo para que compile.
 *
 * Inyectado solo a chiaki-lib vía -include cuando CHIAKI_IS_WIIU (ver
 * lib/CMakeLists.txt).
 */

#pragma once

#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdint.h>
#include <string.h>

/* wut no trae <netinet/ip.h>; takion.c lo incluye en su rama no-Windows. */
#ifndef _NETINET_IP_H
#define _NETINET_IP_H
/* vacío: takion.c solo necesita IP_* de netinet/in.h (definidos por wut). */
#endif

#ifndef AF_INET6
#define AF_INET6 24
#endif

#ifndef IPV6_ADDRSTRLEN
/* INET6_ADDRSTRLEN estándar (46). */
#define IPV6_ADDRSTRLEN 46
#endif

#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN IPV6_ADDRSTRLEN
#endif

/* Tipo mínimo: los helpers inline (set_port/sockaddr_str) solo leen/escriben
 * sin6_port y hacen cast de sin6_addr; nunca se ejecutan con AF_INET6 en Wii U.
 */
#ifndef _CHIAKI_WIIU_SHIM_SOCKADDR_IN6
#define _CHIAKI_WIIU_SHIM_SOCKADDR_IN6
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
#endif

#ifndef SO_RCVTIMEO
/* nsysnet no soporta SO_RCVTIMEO. Se redirige a SO_NBIO (real en wut):
 * - stun.h fija un timeout tras connect() -> SO_NBIO lo ignora, el socket
 *   sigue bloqueando (correcto, la operacion es bloqueante).
 * - holepunch.c "unset" el timeout (tv=0) -> SO_NBIO lo deja en blocking,
 *   que es el comportamiento por defecto buscado.
 * Asi ambas ramas quedan inofensivas en Cafe OS. */
#define SO_RCVTIMEO SO_NBIO
#endif

/* discovery.c usa estos en sus ramas IPv6, que en Cafe OS (IPv4-only) nunca
 * se ejecutan pero deben compilar. static para no romper la linkage entre
 * TUs; attribute unused porque la mayoria de los TUs no los referencian. */
#ifndef IN6ADDR_ANY_INIT
#define IN6ADDR_ANY_INIT { { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } }
#endif
#ifndef IN6ADDR_LOOPBACK_INIT
#define IN6ADDR_LOOPBACK_INIT { { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 } }
#endif
static const struct in6_addr in6addr_any __attribute__((unused)) = IN6ADDR_ANY_INIT;
static const struct in6_addr in6addr_loopback __attribute__((unused)) = IN6ADDR_LOOPBACK_INIT;
