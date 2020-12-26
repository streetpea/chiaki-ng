// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_DISCOVERY_H
#define CHIAKI_DISCOVERY_H

#include "common.h"
#include "thread.h"
#include "stoppipe.h"
#include "log.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef unsigned short sa_family_t;
#else
#include <sys/types.h>
#include <sys/socket.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define CHIAKI_DISCOVERY_PORT_PS4 987
#define CHIAKI_DISCOVERY_PROTOCOL_VERSION_PS4 "00020020"
#define CHIAKI_DISCOVERY_PORT_PS5 9302
#define CHIAKI_DISCOVERY_PROTOCOL_VERSION_PS5 "00030010"

typedef enum chiaki_discovery_cmd_t
{
	CHIAKI_DISCOVERY_CMD_SRCH,
	CHIAKI_DISCOVERY_CMD_WAKEUP
} ChiakiDiscoveryCmd;

typedef struct chiaki_discovery_packet_t
{
	ChiakiDiscoveryCmd cmd;
	char *protocol_version;
	uint64_t user_credential; // for wakeup, this is just the regist key interpreted as hex
} ChiakiDiscoveryPacket;

typedef enum chiaki_discovery_host_state_t
{
	CHIAKI_DISCOVERY_HOST_STATE_UNKNOWN,
	CHIAKI_DISCOVERY_HOST_STATE_READY,
	CHIAKI_DISCOVERY_HOST_STATE_STANDBY
} ChiakiDiscoveryHostState;

const char *chiaki_discovery_host_state_string(ChiakiDiscoveryHostState state);

/**
 * Apply A on all names of string members in ChiakiDiscoveryHost
 */
#define CHIAKI_DISCOVERY_HOST_STRING_FOREACH(A) \
	A(host_addr); \
	A(system_version); \
	A(device_discovery_protocol_version); \
	A(host_name); \
	A(host_type); \
	A(host_id); \
	A(running_app_titleid); \
	A(running_app_name);

typedef struct chiaki_discovery_host_t
{
	// All string members here must be in sync with CHIAKI_DISCOVERY_HOST_STRING_FOREACH
	ChiakiDiscoveryHostState state;
	uint16_t host_request_port;
#define STRING_MEMBER(name) const char *name
	CHIAKI_DISCOVERY_HOST_STRING_FOREACH(STRING_MEMBER)
#undef STRING_MEMBER
} ChiakiDiscoveryHost;

CHIAKI_EXPORT bool chiaki_discovery_host_is_ps5(ChiakiDiscoveryHost *host);

CHIAKI_EXPORT ChiakiTarget chiaki_discovery_host_system_version_target(ChiakiDiscoveryHost *host);

CHIAKI_EXPORT int chiaki_discovery_packet_fmt(char *buf, size_t buf_size, ChiakiDiscoveryPacket *packet);

typedef struct chiaki_discovery_t
{
	ChiakiLog *log;
	chiaki_socket_t socket;
	struct sockaddr local_addr;
} ChiakiDiscovery;

CHIAKI_EXPORT ChiakiErrorCode chiaki_discovery_init(ChiakiDiscovery *discovery, ChiakiLog *log, sa_family_t family);
CHIAKI_EXPORT void chiaki_discovery_fini(ChiakiDiscovery *discovery);
CHIAKI_EXPORT ChiakiErrorCode chiaki_discovery_send(ChiakiDiscovery *discovery, ChiakiDiscoveryPacket *packet, struct sockaddr *addr, size_t addr_size);

typedef void (*ChiakiDiscoveryCb)(ChiakiDiscoveryHost *host, void *user);

typedef struct chiaki_discovery_thread_t
{
	ChiakiDiscovery *discovery;
	ChiakiThread thread;
	ChiakiStopPipe stop_pipe;
	ChiakiDiscoveryCb cb;
	void *cb_user;
} ChiakiDiscoveryThread;

CHIAKI_EXPORT ChiakiErrorCode chiaki_discovery_thread_start(ChiakiDiscoveryThread *thread, ChiakiDiscovery *discovery, ChiakiDiscoveryCb cb, void *cb_user);
CHIAKI_EXPORT ChiakiErrorCode chiaki_discovery_thread_stop(ChiakiDiscoveryThread *thread);

/**
 * Convenience function to send a wakeup packet
 * @param discovery Discovery to send the packet on. May be NULL, in which case a new temporary Discovery will be created
 */
CHIAKI_EXPORT ChiakiErrorCode chiaki_discovery_wakeup(ChiakiLog *log, ChiakiDiscovery *discovery, const char *host, uint64_t user_credential, bool ps5);

#ifdef __cplusplus
}
#endif

#endif // CHIAKI_VIDEORECEIVER_H
