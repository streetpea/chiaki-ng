#ifndef CHIAKI_WASM_PROTOCOL_H
#define CHIAKI_WASM_PROTOCOL_H

#include <stdint.h>

#define CHIAKI_NET_HDR_SIZE 28

#define CHIAKI_NET_SOCKET        1
#define CHIAKI_NET_CLOSE         2
#define CHIAKI_NET_BIND          3
#define CHIAKI_NET_CONNECT       4
#define CHIAKI_NET_SEND          5
#define CHIAKI_NET_SENDTO        6
#define CHIAKI_NET_SETSOCKOPT    7
#define CHIAKI_NET_GETSOCKNAME   8
#define CHIAKI_NET_GETADDRINFO   9
#define CHIAKI_NET_SHUTDOWN      10

#define CHIAKI_NET_REPLY         128
#define CHIAKI_NET_PUSH_DATA     129
#define CHIAKI_NET_PUSH_CONNECTED 130
#define CHIAKI_NET_PUSH_CLOSED   131
#define CHIAKI_NET_PUSH_ERROR    132

#ifdef __cplusplus
extern "C" {
#endif

typedef struct chiaki_net_hdr_t
{
	uint8_t type;
	uint8_t pad[3];
	uint32_t id;
	int32_t fd;
	int32_t a;
	int32_t b;
	int32_t c;
	uint32_t len;
} ChiakiNetHdr;

#ifdef __cplusplus
}
#endif

#endif
