/* Stub de <net/if.h> para compilar miniupnpc contra wut (Wii U).
 *
 * wut no trae net/if.h. miniupnpc.c y miniwget.c hacen el include sin usar
 * ningún símbolo. minissdpc.c sí usa algunos símbolos (ioctl SIOCGIFADDR,
 * if_nametoindex, struct ifreq/ifr_addr, ip_mreqn, PF_INET6) pero solo dentro
 * del runtime-guard if(multicastif && multicastif[0] != '\0'), que chiaki
 * nunca activa (no pasa nombre de interfaz multicast). Aquí se declaran de
 * forma mínima para que compile; nunca se ejecutan en Wii U.
 */

#ifndef _NET_IF_H
#define _NET_IF_H

#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define IFNAMSIZ 16

/* Compat ioctl de interfaz (nunca se ejecuta en runtime desde chiaki). */
#ifndef SIOCGIFADDR
#define SIOCGIFADDR 0x8915
#endif

#ifndef PF_INET6
#define PF_INET6 24
#endif

struct ifreq {
	char ifr_name[IFNAMSIZ];
	union {
		struct sockaddr ifr_addr;
		short ifr_flags;
		int ifr_ifindex;
		char ifr_pad[24];
	};
};

/* ip_mreqn: solo se usa si HAS_IP_MREQN (no definido en wut), pero se
 * declara el tipo por si acaso. */
#ifndef _NET_IF_H_IP_MREQN
#define _NET_IF_H_IP_MREQN
struct ip_mreqn {
	struct in_addr imr_address;
	int imr_ifindex;
};
#endif

/* if_nametoindex: wut no lo exporta; se declara aquí. La rama que lo usa
 * (multicastif por nombre de interfaz) no se ejecuta desde chiaki. */
extern unsigned int if_nametoindex(const char *ifname);

#endif /* _NET_IF_H */
