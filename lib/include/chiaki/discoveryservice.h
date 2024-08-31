// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL


#ifndef CHIAKI_DISCOVERYSERVICE_H
#define CHIAKI_DISCOVERYSERVICE_H

#include "discovery.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ChiakiDiscoveryServiceCb)(ChiakiDiscoveryHost *hosts, size_t hosts_count, void *user);

typedef struct chiaki_discovery_service_options_t
{
	size_t hosts_max;
	uint64_t host_drop_pings;
	uint64_t ping_ms;
	uint64_t ping_initial_ms;
	struct sockaddr_storage *send_addr;
	size_t send_addr_size;
	struct sockaddr_storage *broadcast_addrs;
	size_t broadcast_num;
	char *send_host;
	ChiakiDiscoveryServiceCb cb;
	void *cb_user;
} ChiakiDiscoveryServiceOptions;

typedef struct chiaki_discovery_service_host_discovery_info_t
{
	uint64_t last_ping_index;
} ChiakiDiscoveryServiceHostDiscoveryInfo;

typedef struct chiaki_discovery_service_t
{
	ChiakiLog *log;
	ChiakiDiscoveryServiceOptions options;
	ChiakiDiscovery discovery;

	uint64_t ping_index;
	ChiakiDiscoveryHost *hosts;
	ChiakiDiscoveryServiceHostDiscoveryInfo *host_discovery_infos;
	size_t hosts_count;
	ChiakiMutex state_mutex;

	ChiakiThread thread;
	ChiakiBoolPredCond stop_cond;
} ChiakiDiscoveryService;

CHIAKI_EXPORT ChiakiErrorCode chiaki_discovery_service_init(ChiakiDiscoveryService *service, ChiakiDiscoveryServiceOptions *options, ChiakiLog *log);
CHIAKI_EXPORT void chiaki_discovery_service_fini(ChiakiDiscoveryService *service);

#ifdef __cplusplus
}
#endif

#endif //CHIAKI_DISCOVERYSERVICE_H
