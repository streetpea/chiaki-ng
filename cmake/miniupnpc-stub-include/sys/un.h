/* Stub de <sys/un.h> para compilar miniupnpc contra wut (Wii U).
 *
 * wut no trae sys/un.h. minissdpc.c lo incluye en su rama no-Win32 pero solo
 * usa sockaddr_un/AF_UNIX/sun_path, en ramas de daemon local que chiaki nunca
 * activa (usa la interfaz de red estándar, no sockets Unix).
 */

#ifndef _SYS_UN_H
#define _SYS_UN_H

#include <stdint.h>

#ifndef AF_UNIX
#define AF_UNIX 1
#endif

struct sockaddr_un {
	uint16_t sun_family;
	char sun_path[108];
};

#endif /* _SYS_UN_H */
