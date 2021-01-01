// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifdef __SWITCH__
#include <switch.h>
#endif

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>

#include <discoverymanager.h>

#define PING_MS 500
#define HOSTS_MAX 16
#define DROP_PINGS 3

static void Discovery(ChiakiDiscoveryHost *discovered_hosts, size_t hosts_count, void *user)
{
	DiscoveryManager *dm = (DiscoveryManager *)user;
	for(size_t i = 0; i < hosts_count; i++)
	{
		dm->DiscoveryCB(discovered_hosts + i);
	}
}

DiscoveryManager::DiscoveryManager()
{
	this->settings = Settings::GetInstance();
	this->log = this->settings->GetLogger();
}

DiscoveryManager::~DiscoveryManager()
{
	// join discovery thread
	if(this->service_enable)
	{
		SetService(false);
	}

	chiaki_discovery_fini(&this->discovery);
}

void DiscoveryManager::SetService(bool enable)
{
	if(this->service_enable == enable)
	{
		return;
	}

	this->service_enable = enable;

	if(enable)
	{
		ChiakiDiscoveryServiceOptions options;
		options.ping_ms = PING_MS;
		options.hosts_max = HOSTS_MAX;
		options.host_drop_pings = DROP_PINGS;
		options.cb = Discovery;
		options.cb_user = this;

		sockaddr_in addr = {};
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = GetIPv4BroadcastAddr();
		options.send_addr = reinterpret_cast<sockaddr *>(&addr);
		options.send_addr_size = sizeof(addr);

		ChiakiErrorCode err = chiaki_discovery_service_init(&this->service, &options, log);
		if(err != CHIAKI_ERR_SUCCESS)
		{
			this->service_enable = false;
			CHIAKI_LOGE(this->log, "DiscoveryManager failed to init Discovery Service");
			return;
		}
	}
	else
	{
		chiaki_discovery_service_fini(&this->service);
	}
}

uint32_t DiscoveryManager::GetIPv4BroadcastAddr()
{
#ifdef __SWITCH__
	uint32_t current_addr, subnet_mask;
	// init nintendo net interface service
	Result rc = nifmInitialize(NifmServiceType_User);
	if(R_SUCCEEDED(rc))
	{
		// read current IP and netmask
		rc = nifmGetCurrentIpConfigInfo(
			&current_addr, &subnet_mask,
			NULL, NULL, NULL);
		nifmExit();
	}
	else
	{
		CHIAKI_LOGE(this->log, "Failed to get nintendo nifmGetCurrentIpConfigInfo");
		return 1;
	}
	return current_addr | (~subnet_mask);
#else
	return 0xffffffff;
#endif
}

int DiscoveryManager::Send(struct sockaddr *host_addr, size_t host_addr_len)
{
	if(!host_addr)
	{
		CHIAKI_LOGE(log, "Null sockaddr");
		return 1;
	}
	ChiakiDiscoveryPacket packet;
	memset(&packet, 0, sizeof(packet));
	packet.cmd = CHIAKI_DISCOVERY_CMD_SRCH;

	chiaki_discovery_send(&this->discovery, &packet, this->host_addr, this->host_addr_len);
	return 0;
}

int DiscoveryManager::Send(const char *discover_ip_dest)
{
	struct addrinfo *host_addrinfos;
	int r = getaddrinfo(discover_ip_dest, NULL, NULL, &host_addrinfos);
	if(r != 0)
	{
		CHIAKI_LOGE(log, "getaddrinfo failed");
		return 1;
	}

	struct sockaddr *host_addr = nullptr;
	socklen_t host_addr_len = 0;

	for(struct addrinfo *ai = host_addrinfos; ai; ai = ai->ai_next)
	{
		if(ai->ai_protocol != IPPROTO_UDP)
			continue;
		if(ai->ai_family != AF_INET) // TODO: IPv6
			continue;

		this->host_addr_len = ai->ai_addrlen;
		this->host_addr = (struct sockaddr *)malloc(host_addr_len);
		if(!this->host_addr)
			break;
		memcpy(this->host_addr, ai->ai_addr, this->host_addr_len);
	}

	freeaddrinfo(host_addrinfos);

	if(!this->host_addr)
	{
		CHIAKI_LOGE(log, "Failed to get addr for hostname");
		return 1;
	}
	return DiscoveryManager::Send(this->host_addr, this->host_addr_len);
}

int DiscoveryManager::Send()
{
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = GetIPv4BroadcastAddr();

	this->host_addr_len = sizeof(sockaddr_in);
	this->host_addr = (struct sockaddr *)malloc(host_addr_len);
	memcpy(this->host_addr, &addr, this->host_addr_len);

	return DiscoveryManager::Send(this->host_addr, this->host_addr_len);
}

void DiscoveryManager::DiscoveryCB(ChiakiDiscoveryHost *discovered_host)
{
	// the user ptr is passed as
	// chiaki_discovery_thread_start arg

	std::string key = discovered_host->host_name;
	Host *host = this->settings->GetOrCreateHost(&key);

	CHIAKI_LOGI(this->log, "--");
	CHIAKI_LOGI(this->log, "Discovered Host:");
	CHIAKI_LOGI(this->log, "State:                             %s", chiaki_discovery_host_state_string(discovered_host->state));

	host->state = discovered_host->state;
	host->discovered = true;

	// add host ptr to list
	if(discovered_host->system_version && discovered_host->device_discovery_protocol_version)
	{
		// example: 07020001
		ChiakiTarget target = chiaki_discovery_host_system_version_target(discovered_host);
		host->SetChiakiTarget(target);

		CHIAKI_LOGI(this->log, "System Version:                    %s", discovered_host->system_version);
		CHIAKI_LOGI(this->log, "Device Discovery Protocol Version: %s", discovered_host->device_discovery_protocol_version);
		CHIAKI_LOGI(this->log, "PlayStation ChiakiTarget Version:  %d", target);
	}

	if(discovered_host->host_request_port)
		CHIAKI_LOGI(this->log, "Request Port:                      %hu", (unsigned short)discovered_host->host_request_port);

	if(discovered_host->host_addr)
	{
		host->host_addr = discovered_host->host_addr;
		CHIAKI_LOGI(this->log, "Host Addr:                         %s", discovered_host->host_addr);
	}

	if(discovered_host->host_name)
	{
		host->host_name = discovered_host->host_name;
		CHIAKI_LOGI(this->log, "Host Name:                         %s", discovered_host->host_name);
	}

	if(discovered_host->host_type)
		CHIAKI_LOGI(this->log, "Host Type:                         %s", discovered_host->host_type);

	if(discovered_host->host_id)
		CHIAKI_LOGI(this->log, "Host ID:                           %s", discovered_host->host_id);

	if(discovered_host->running_app_titleid)
		CHIAKI_LOGI(this->log, "Running App Title ID:              %s", discovered_host->running_app_titleid);

	if(discovered_host->running_app_name)
		CHIAKI_LOGI(this->log, "Running App Name:                  %s", discovered_host->running_app_name);

	CHIAKI_LOGI(this->log, "--");
}
