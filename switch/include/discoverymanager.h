// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_DISCOVERYMANAGER_H
#define CHIAKI_DISCOVERYMANAGER_H

#include <map>
#include <string>

#include <chiaki/discoveryservice.h>
#include <chiaki/log.h>

#include "host.h"
#include "settings.h"

static void Discovery(ChiakiDiscoveryHost *, void *);

class DiscoveryManager
{
	private:
		Settings * settings = nullptr;
		ChiakiLog * log = nullptr;
		ChiakiDiscoveryService service;
		ChiakiDiscovery discovery;
		struct sockaddr * host_addr = nullptr;
		size_t host_addr_len = 0;
		uint32_t GetIPv4BroadcastAddr();
		bool service_enable = false;

	public:
		typedef enum hoststate
		{
			UNKNOWN,
			READY,
			STANDBY,
			SHUTDOWN,
		} HostState;

		DiscoveryManager();
		~DiscoveryManager();
		void SetService(bool);
		int Send();
		int Send(struct sockaddr * host_addr, size_t host_addr_len);
		int Send(const char *);
		void DiscoveryCB(ChiakiDiscoveryHost *);
};

#endif //CHIAKI_DISCOVERYMANAGER_H
