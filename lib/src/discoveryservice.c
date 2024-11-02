// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/discoveryservice.h>

#include <string.h>
#include <assert.h>
#include <stdlib.h>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <netdb.h>
#include <netinet/in.h>
#endif

static void *discovery_service_thread_func(void *user);
static void discovery_service_ping(ChiakiDiscoveryService *service);
static void discovery_service_drop_old_hosts(ChiakiDiscoveryService *service);
static void discovery_service_host_received(ChiakiDiscoveryHost *host, void *user);
static void discovery_service_report_state(ChiakiDiscoveryService *service);

CHIAKI_EXPORT ChiakiErrorCode chiaki_discovery_service_init(ChiakiDiscoveryService *service, ChiakiDiscoveryServiceOptions *options, ChiakiLog *log)
{
	service->log = log;
	service->options = *options;
	service->ping_index = 0;

	service->hosts = calloc(service->options.hosts_max, sizeof(ChiakiDiscoveryHost));
	if(!service->hosts)
		return CHIAKI_ERR_MEMORY;

	ChiakiErrorCode err;
	service->host_discovery_infos = calloc(service->options.hosts_max, sizeof(ChiakiDiscoveryServiceHostDiscoveryInfo));
	if(!service->host_discovery_infos)
	{
		err = CHIAKI_ERR_MEMORY;
		goto error_hosts;
	}

	service->hosts_count = 0;

	err = chiaki_mutex_init(&service->state_mutex, false);
	if(err != CHIAKI_ERR_SUCCESS)
		goto error_host_discovery_infos;

	service->options.send_addr = malloc(service->options.send_addr_size);
	if(!service->options.send_addr)
	{
		err = CHIAKI_ERR_MEMORY;
		goto error_state_mutex;
	}
	memcpy(service->options.send_addr, options->send_addr, service->options.send_addr_size);

	service->options.broadcast_addrs = NULL;
	service->options.broadcast_num = options->broadcast_num;
	if(options->broadcast_num > 0)
	{
		service->options.broadcast_addrs = malloc(service->options.broadcast_num * sizeof(struct sockaddr_storage));
		if(!service->options.broadcast_addrs)
		{
			err = CHIAKI_ERR_MEMORY;
			goto error_state_mutex;
		}
		memcpy(service->options.broadcast_addrs, options->broadcast_addrs, service->options.broadcast_num * sizeof(struct sockaddr_storage));
	}

	if(service->options.send_host)
	{
		service->options.send_host = strdup(service->options.send_host);
		if(!service->options.send_host)
		{
			err = CHIAKI_ERR_MEMORY;
			goto error_send_addr;
		}
	}

	err = chiaki_discovery_init(&service->discovery, log, ((struct sockaddr *)service->options.send_addr)->sa_family);
	if(err != CHIAKI_ERR_SUCCESS)
		goto error_send_addr;

	err = chiaki_bool_pred_cond_init(&service->stop_cond);
	if(err != CHIAKI_ERR_SUCCESS)
		goto error_discovery;

	err = chiaki_thread_create(&service->thread, discovery_service_thread_func, service);
	if(err != CHIAKI_ERR_SUCCESS)
		goto error_stop_cond;

	chiaki_thread_set_name(&service->thread, "Chiaki Discovery Service");

	return CHIAKI_ERR_SUCCESS;
error_stop_cond:
	chiaki_bool_pred_cond_fini(&service->stop_cond);
error_discovery:
	chiaki_discovery_fini(&service->discovery);
error_send_addr:
	free(service->options.broadcast_addrs);
	free(service->options.send_addr);
	free(service->options.send_host);
error_state_mutex:
	chiaki_mutex_fini(&service->state_mutex);
error_host_discovery_infos:
	free(service->host_discovery_infos);
error_hosts:
	free(service->hosts);
	return err;
}

CHIAKI_EXPORT void chiaki_discovery_service_fini(ChiakiDiscoveryService *service)
{
	chiaki_bool_pred_cond_signal(&service->stop_cond);
	chiaki_thread_join(&service->thread, NULL);
	chiaki_bool_pred_cond_fini(&service->stop_cond);
	chiaki_discovery_fini(&service->discovery);
	chiaki_mutex_fini(&service->state_mutex);
	free(service->options.send_addr);
	free(service->options.send_host);
	if(service->options.broadcast_addrs)
		free(service->options.broadcast_addrs);

	for(size_t i=0; i<service->hosts_count; i++)
	{
		ChiakiDiscoveryHost *host = &service->hosts[i];
#define FREE_STRING(name) free((char *)host->name);
		CHIAKI_DISCOVERY_HOST_STRING_FOREACH(FREE_STRING)
#undef FREE_STRING
	}

	free(service->host_discovery_infos);
	free(service->hosts);
}

static void *discovery_service_thread_func(void *user)
{
	ChiakiDiscoveryService *service = user;

	ChiakiErrorCode err = chiaki_bool_pred_cond_lock(&service->stop_cond);
	if(err != CHIAKI_ERR_SUCCESS)
		return NULL;

	ChiakiDiscoveryThread discovery_thread;
	err = chiaki_discovery_thread_start(&discovery_thread, &service->discovery, discovery_service_host_received, service);
	if(err != CHIAKI_ERR_SUCCESS)
		goto beach;

	err = chiaki_bool_pred_cond_timedwait(&service->stop_cond, service->options.ping_initial_ms);

	while(err == CHIAKI_ERR_TIMEOUT)
	{
		discovery_service_ping(service);
		err = chiaki_bool_pred_cond_timedwait(&service->stop_cond, service->options.ping_ms);
	}

	chiaki_discovery_thread_stop(&discovery_thread);

beach:
	chiaki_bool_pred_cond_unlock(&service->stop_cond);
	return NULL;
}

static void discovery_service_ping(ChiakiDiscoveryService *service)
{
	ChiakiErrorCode err = chiaki_mutex_lock(&service->state_mutex);
	assert(err == CHIAKI_ERR_SUCCESS);

	service->ping_index++;
	discovery_service_drop_old_hosts(service);

	chiaki_mutex_unlock(&service->state_mutex);

	if(service->options.send_host)
	{
		struct addrinfo *host_addrinfos;
		struct addrinfo hints;
		memset(&hints, 0, sizeof(hints));
		hints.ai_socktype = SOCK_DGRAM;
		hints.ai_family = ((struct sockaddr *)service->options.send_addr)->sa_family;
		int r = getaddrinfo(service->options.send_host, NULL, &hints, &host_addrinfos);
		if(r != 0)
		{
			CHIAKI_LOGE(service->log, "getaddrinfo failed");
			return;
		}

		bool ok = false;
		for(struct addrinfo *ai=host_addrinfos; ai; ai=ai->ai_next)
		{
			if(ai->ai_family != AF_INET && ai->ai_family != AF_INET6)
				continue;
			ok = true;
			memcpy(service->options.send_addr, ai->ai_addr, ai->ai_addrlen);
		}
		freeaddrinfo(host_addrinfos);

		if(!ok)
		{
			CHIAKI_LOGE(service->log, "Failed to get addr for hostname");
			return;
		}

		free(service->options.send_host);
		service->options.send_host = NULL;
	}

	CHIAKI_LOGV(service->log, "Discovery Service sending ping");
	ChiakiDiscoveryPacket packet = { 0 };
	bool send_extra_broadcast = false;
	packet.cmd = CHIAKI_DISCOVERY_CMD_SRCH;
	packet.protocol_version = CHIAKI_DISCOVERY_PROTOCOL_VERSION_PS4;
	if(((struct sockaddr *)service->options.send_addr)->sa_family == AF_INET)
	{
		((struct sockaddr_in *)service->options.send_addr)->sin_port = htons(CHIAKI_DISCOVERY_PORT_PS4);
		if(((struct sockaddr_in *)service->options.send_addr)->sin_addr.s_addr == 0xffffffff)
			send_extra_broadcast = true;
	}
	else if(((struct sockaddr *)service->options.send_addr)->sa_family == AF_INET6)
		((struct sockaddr_in6 *)service->options.send_addr)->sin6_port = htons(CHIAKI_DISCOVERY_PORT_PS4);
	else
	{
		CHIAKI_LOGE(service->log, "Discovery Service send_addr has unknown sa_family");
		return;
	}
	err = chiaki_discovery_send(&service->discovery, &packet, (struct sockaddr *)service->options.send_addr, service->options.send_addr_size);
	if(err != CHIAKI_ERR_SUCCESS)
		CHIAKI_LOGE(service->log, "Discovery Service failed to send ping for PS4");
	if(send_extra_broadcast)
	{
		for(int i = 0; i < service->options.broadcast_num; i++)
		{
			((struct sockaddr_in *)(&service->options.broadcast_addrs[i]))->sin_port = htons(CHIAKI_DISCOVERY_PORT_PS4);
			err = chiaki_discovery_send(&service->discovery, &packet, (struct sockaddr *)&service->options.broadcast_addrs[i], service->options.send_addr_size);
			if(err != CHIAKI_ERR_SUCCESS)
				CHIAKI_LOGE(service->log, "Discovery Service failed to send extra broadcast ping for PS4");
			else
			{
				char addr_string[INET_ADDRSTRLEN];
				if (!inet_ntop(((struct sockaddr_in *)(&service->options.broadcast_addrs[i]))->sin_family, &(((struct sockaddr_in *)(&service->options.broadcast_addrs[i]))->sin_addr), addr_string, sizeof(addr_string)))
					CHIAKI_LOGE(service->log, "Discovery Service error with inet_ntop");
				else
					CHIAKI_LOGV(service->log, "Discovery Service pinged %s", addr_string);
			}
		}
	}
	packet.protocol_version = CHIAKI_DISCOVERY_PROTOCOL_VERSION_PS5;
	if(((struct sockaddr *)service->options.send_addr)->sa_family == AF_INET)
		((struct sockaddr_in *)service->options.send_addr)->sin_port = htons(CHIAKI_DISCOVERY_PORT_PS5);
	else if(((struct sockaddr *)service->options.send_addr)->sa_family == AF_INET6)
		((struct sockaddr_in6 *)service->options.send_addr)->sin6_port = htons(CHIAKI_DISCOVERY_PORT_PS5);
	err = chiaki_discovery_send(&service->discovery, &packet, (struct sockaddr *)service->options.send_addr, service->options.send_addr_size);
	if(err != CHIAKI_ERR_SUCCESS)
		CHIAKI_LOGE(service->log, "Discovery Service failed to send ping for PS5");
	if(send_extra_broadcast)
	{
		for(int i = 0; i < service->options.broadcast_num; i++)
		{
			((struct sockaddr_in *)(&service->options.broadcast_addrs[i]))->sin_port = htons(CHIAKI_DISCOVERY_PORT_PS5);
			err = chiaki_discovery_send(&service->discovery, &packet, (struct sockaddr *)&service->options.broadcast_addrs[i], service->options.send_addr_size);
			if(err != CHIAKI_ERR_SUCCESS)
				CHIAKI_LOGE(service->log, "Discovery Service failed to send extra broadcast ping for PS5");
		}
	}
}

static void discovery_service_drop_old_hosts(ChiakiDiscoveryService *service)
{
	// service->state_mutex must be locked

	bool change = false;

	for(size_t i=0; i<service->hosts_count; i++)
	{
		if(service->host_discovery_infos[i].last_ping_index + service->options.host_drop_pings >= service->ping_index)
			continue;

		ChiakiDiscoveryHost *host = &service->hosts[i];
		CHIAKI_LOGI(service->log, "Discovery Service: Host with id %s is no longer available", host->host_id ? host->host_id : "");

#define FREE_STRING(name) do { free((char *)host->name); } while(0)
		CHIAKI_DISCOVERY_HOST_STRING_FOREACH(FREE_STRING)
#undef FREE_STRING

		if(i < service->hosts_count - 1)
		{
			memmove(service->host_discovery_infos + i,
					service->host_discovery_infos + i + 1,
					sizeof(ChiakiDiscoveryServiceHostDiscoveryInfo) * (service->hosts_count - i - 1));

			memmove(service->hosts + i,
					service->hosts + i + 1,
					sizeof(ChiakiDiscoveryHost) * (service->hosts_count - i - 1));
		}

		change = true;
		if(i > 0)
			i--;
		service->hosts_count--;
	}

	if(change)
		discovery_service_report_state(service);
}

static void discovery_service_host_received(ChiakiDiscoveryHost *host, void *user)
{
	ChiakiDiscoveryService *service = user;

	ChiakiErrorCode err = chiaki_mutex_lock(&service->state_mutex);
	assert(err == CHIAKI_ERR_SUCCESS);

	if(!host->host_id)
	{
		CHIAKI_LOGE(service->log, "Discovery Service received host without id");
		chiaki_mutex_unlock(&service->state_mutex);
		return;
	}
	CHIAKI_LOGV(service->log, "Discovery Service Received host with id %s", host->host_id);

	bool change = false;

	size_t index = SIZE_MAX;
	for(size_t i=0; i<service->hosts_count; i++)
	{
		if(!service->hosts[i].host_id)
			continue;

		if(strcmp(service->hosts[i].host_id, host->host_id) == 0)
		{
			index = i;
			break;
		}
	}

	if(index == SIZE_MAX)
	{
		if(service->hosts_count == service->options.hosts_max)
		{
			CHIAKI_LOGE(service->log, "Discovery Service received new host, but no space available");
			goto rzcon;
		}

		CHIAKI_LOGI(service->log, "Discovery Service detected new host with id %s", host->host_id);

		change = true;
		index = service->hosts_count++;
		memset(&service->hosts[index], 0, sizeof(ChiakiDiscoveryHost));
	}

	service->host_discovery_infos[index].last_ping_index = service->ping_index;

	ChiakiDiscoveryHost *host_slot = &service->hosts[index];

	if(host_slot->state != host->state || host_slot->host_request_port != host->host_request_port)
		change = true;

	host_slot->state = host->state;
	host_slot->host_request_port = host->host_request_port;

#define UPDATE_STRING(name) do { \
		if(host_slot->name && host->name && strcmp(host_slot->name, host->name) == 0) \
            break; \
		if(!host_slot->name && !host->name) \
			break; \
		change = true; \
		if(host_slot->name) \
			free((char *)host_slot->name); \
		if(host->name) \
			host_slot->name = strdup(host->name); \
		else \
			host_slot->name = NULL; \
	} while(0)

	CHIAKI_DISCOVERY_HOST_STRING_FOREACH(UPDATE_STRING)

#undef UPDATE_STRING

	if(change)
		discovery_service_report_state(service);

rzcon:
	chiaki_mutex_unlock(&service->state_mutex);
}

static void discovery_service_report_state(ChiakiDiscoveryService *service)
{
	// service->state_mutex must be locked
	if(service->options.cb)
		service->options.cb(service->hosts, service->hosts_count, service->options.cb_user);
}
