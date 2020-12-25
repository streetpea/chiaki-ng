// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "utils.h"

#include <chiaki/discovery.h>
#include <chiaki/http.h>
#include <chiaki/log.h>

#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#endif

const char *chiaki_discovery_host_state_string(ChiakiDiscoveryHostState state)
{
	switch(state)
	{
		case CHIAKI_DISCOVERY_HOST_STATE_READY:
			return "ready";
		case CHIAKI_DISCOVERY_HOST_STATE_STANDBY:
			return "standby";
		default:
			return "unknown";
	}
}

CHIAKI_EXPORT int chiaki_discovery_packet_fmt(char *buf, size_t buf_size, ChiakiDiscoveryPacket *packet)
{
	if(!packet->protocol_version)
		return -1;
	switch(packet->cmd)
	{
		case CHIAKI_DISCOVERY_CMD_SRCH:
			return snprintf(buf, buf_size, "SRCH * HTTP/1.1\ndevice-discovery-protocol-version:%s\n",
							packet->protocol_version);
		case CHIAKI_DISCOVERY_CMD_WAKEUP:
			return snprintf(buf, buf_size,
				"WAKEUP * HTTP/1.1\n"
				"client-type:vr\n"
				"auth-type:R\n"
				"model:w\n"
				"app-type:r\n"
				"user-credential:%llu\n"
				"device-discovery-protocol-version:%s\n",
				(unsigned long long)packet->user_credential, packet->protocol_version);
		default:
			return -1;
	}
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_discovery_srch_response_parse(ChiakiDiscoveryHost *response, struct sockaddr *addr, char *addr_buf, size_t addr_buf_size, char *buf, size_t buf_size)
{
	ChiakiHttpResponse http_response;
	ChiakiErrorCode err = chiaki_http_response_parse(&http_response, buf, buf_size);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	memset(response, 0, sizeof(*response));

	response->host_addr = sockaddr_str(addr, addr_buf, addr_buf_size);

	switch(http_response.code)
	{
		case 200:
			response->state = CHIAKI_DISCOVERY_HOST_STATE_READY;
			break;
		case 620:
			response->state = CHIAKI_DISCOVERY_HOST_STATE_STANDBY;
			break;
		default:
			response->state = CHIAKI_DISCOVERY_HOST_STATE_UNKNOWN;
			break;
	}

	for(ChiakiHttpHeader *header = http_response.headers; header; header=header->next)
	{
		if(strcmp(header->key, "system-version") == 0)
			response->system_version = header->value;
		else if(strcmp(header->key, "device-discovery-protocol-version") == 0)
			response->device_discovery_protocol_version = header->value;
		else if(strcmp(header->key, "host-request-port") == 0)
			response->host_request_port = (uint16_t)strtoul(header->value, NULL, 0);
		else if(strcmp(header->key, "host-name") == 0)
			response->host_name = header->value;
		else if(strcmp(header->key, "host-type") == 0)
			response->host_type = header->value;
		else if(strcmp(header->key, "host-id") == 0)
			response->host_id = header->value;
		else if(strcmp(header->key, "running-app-titleid") == 0)
			response->running_app_titleid = header->value;
		else if(strcmp(header->key, "running-app-name") == 0)
			response->running_app_name = header->value;
		//else
		//	printf("unknown %s: %s\n", header->key, header->value);
	}

	chiaki_http_response_fini(&http_response);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_discovery_init(ChiakiDiscovery *discovery, ChiakiLog *log, sa_family_t family)
{
	if(family != AF_INET && family != AF_INET6)
		return CHIAKI_ERR_INVALID_DATA;

	discovery->log = log;

	discovery->socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if(CHIAKI_SOCKET_IS_INVALID(discovery->socket))
	{
		CHIAKI_LOGE(discovery->log, "Discovery failed to create socket");
		return CHIAKI_ERR_NETWORK;
	}

	memset(&discovery->local_addr, 0, sizeof(discovery->local_addr));
	discovery->local_addr.sa_family = family;
	if(family == AF_INET6)
	{
#ifndef __SWITCH__
		struct in6_addr anyaddr = IN6ADDR_ANY_INIT;
#endif
		struct sockaddr_in6 *addr = (struct sockaddr_in6 *)&discovery->local_addr;
#ifndef __SWITCH__
		addr->sin6_addr = anyaddr;
#endif
		addr->sin6_port = htons(0);
	}
	else // AF_INET
	{
		struct sockaddr_in *addr = (struct sockaddr_in *)&discovery->local_addr;
		addr->sin_addr.s_addr = htonl(INADDR_ANY);
		addr->sin_port = htons(0);
	}

	int r = bind(discovery->socket, &discovery->local_addr, sizeof(discovery->local_addr));
	if(r < 0)
	{
		CHIAKI_LOGE(discovery->log, "Discovery failed to bind");
		CHIAKI_SOCKET_CLOSE(discovery->socket);
		return CHIAKI_ERR_NETWORK;
	}

	const int broadcast = 1;
	r = setsockopt(discovery->socket, SOL_SOCKET, SO_BROADCAST, (const void *)&broadcast, sizeof(broadcast));
	if(r < 0)
		CHIAKI_LOGE(discovery->log, "Discovery failed to setsockopt SO_BROADCAST");

//#ifdef __FreeBSD__
//	const int onesbcast = 1;
//	r = setsockopt(discovery->socket, IPPROTO_IP, IP_ONESBCAST, &onesbcast, sizeof(onesbcast));
//	if(r < 0)
//		CHIAKI_LOGE(discovery->log, "Discovery failed to setsockopt IP_ONESBCAST");
//#endif

	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT void chiaki_discovery_fini(ChiakiDiscovery *discovery)
{
	CHIAKI_SOCKET_CLOSE(discovery->socket);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_discovery_send(ChiakiDiscovery *discovery, ChiakiDiscoveryPacket *packet, struct sockaddr *addr, size_t addr_size)
{
	if(addr->sa_family != discovery->local_addr.sa_family)
		return CHIAKI_ERR_INVALID_DATA;

	char buf[512];
	int len = chiaki_discovery_packet_fmt(buf, sizeof(buf), packet);
	if(len < 0)
		return CHIAKI_ERR_UNKNOWN;
	if((size_t)len >= sizeof(buf))
		return CHIAKI_ERR_BUF_TOO_SMALL;

	CHIAKI_LOGV(discovery->log, "Discovery sending:");
	chiaki_log_hexdump(discovery->log, CHIAKI_LOG_VERBOSE, (const uint8_t *)buf, (size_t)len + 1);
	int rc = sendto_broadcast(discovery->log, discovery->socket, buf, (size_t)len + 1, 0, addr, addr_size);
	if(rc < 0)
	{
		CHIAKI_LOGE(discovery->log, "Discovery failed to send: %s", strerror(errno));
		return CHIAKI_ERR_NETWORK;
	}

	return CHIAKI_ERR_SUCCESS;
}

static void *discovery_thread_func(void *user);

CHIAKI_EXPORT ChiakiErrorCode chiaki_discovery_thread_start(ChiakiDiscoveryThread *thread, ChiakiDiscovery *discovery, ChiakiDiscoveryCb cb, void *cb_user)
{
	thread->discovery = discovery;
	thread->cb = cb;
	thread->cb_user = cb_user;

	ChiakiErrorCode err = chiaki_stop_pipe_init(&thread->stop_pipe);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(discovery->log, "Discovery (thread) failed to create pipe");
		return err;
	}

	err = chiaki_thread_create(&thread->thread, discovery_thread_func, thread);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		chiaki_stop_pipe_fini(&thread->stop_pipe);
		return err;
	}

	chiaki_thread_set_name(&thread->thread, "Chiaki Discovery");

	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_discovery_thread_stop(ChiakiDiscoveryThread *thread)
{
	chiaki_stop_pipe_stop(&thread->stop_pipe);
	ChiakiErrorCode err = chiaki_thread_join(&thread->thread, NULL);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	chiaki_stop_pipe_fini(&thread->stop_pipe);
	return CHIAKI_ERR_SUCCESS;
}

static void *discovery_thread_func(void *user)
{
	ChiakiDiscoveryThread *thread = user;
	ChiakiDiscovery *discovery = thread->discovery;

	while(1)
	{
		ChiakiErrorCode err = chiaki_stop_pipe_select_single(&thread->stop_pipe, discovery->socket, false, UINT64_MAX);
		if(err == CHIAKI_ERR_CANCELED)
			break;
		if(err != CHIAKI_ERR_SUCCESS)
		{
			CHIAKI_LOGE(discovery->log, "Discovery thread failed to select");
			break;
		}

		char buf[512];
		struct sockaddr client_addr;
		socklen_t client_addr_size = sizeof(client_addr);
		int n = recvfrom(discovery->socket, buf, sizeof(buf) - 1, 0, &client_addr, &client_addr_size);
		if(n < 0)
		{
			CHIAKI_LOGE(discovery->log, "Discovery thread failed to read from socket");
			break;
		}

		if(n == 0)
			continue;

		if(n > sizeof(buf) - 1)
			n = sizeof(buf) - 1;

		buf[n] = '\00';

		//CHIAKI_LOGV(discovery->log, "Discovery received:\n%s", buf);
		//chiaki_log_hexdump_raw(discovery->log, CHIAKI_LOG_VERBOSE, (const uint8_t *)buf, n);

		char addr_buf[64];
		ChiakiDiscoveryHost response;
		err = chiaki_discovery_srch_response_parse(&response, &client_addr, addr_buf, sizeof(addr_buf), buf, n);
		if(err != CHIAKI_ERR_SUCCESS)
		{
			CHIAKI_LOGI(discovery->log, "Discovery Response invalid");
			continue;
		}

		if(thread->cb)
			thread->cb(&response, thread->cb_user);
	}

	return NULL;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_discovery_wakeup(ChiakiLog *log, ChiakiDiscovery *discovery, const char *host, uint64_t user_credential, bool ps5)
{
	struct addrinfo *addrinfos;
	int r = getaddrinfo(host, NULL, NULL, &addrinfos); // TODO: this blocks, use something else
	if(r != 0)
	{
		CHIAKI_LOGE(log, "DiscoveryManager failed to getaddrinfo for wakeup");
		return CHIAKI_ERR_NETWORK;
	}
	struct sockaddr addr = { 0 };
	socklen_t addr_len = 0;
	for(struct addrinfo *ai=addrinfos; ai; ai=ai->ai_next)
	{
		if(ai->ai_family != AF_INET)
			continue;
		//if(ai->ai_protocol != IPPROTO_UDP)
		//	continue;
		if(ai->ai_addrlen > sizeof(addr))
			continue;
		memcpy(&addr, ai->ai_addr, ai->ai_addrlen);
		addr_len = ai->ai_addrlen;
		break;
	}
	freeaddrinfo(addrinfos);

	if(!addr_len)
	{
		CHIAKI_LOGE(log, "DiscoveryManager failed to get suitable address from getaddrinfo for wakeup");
		return CHIAKI_ERR_UNKNOWN;
	}

	((struct sockaddr_in *)&addr)->sin_port = htons(ps5 ? CHIAKI_DISCOVERY_PORT_PS5 : CHIAKI_DISCOVERY_PORT_PS4);

	ChiakiDiscoveryPacket packet = { 0 };
	packet.cmd = CHIAKI_DISCOVERY_CMD_WAKEUP;
	packet.protocol_version = ps5 ? CHIAKI_DISCOVERY_PROTOCOL_VERSION_PS5 : CHIAKI_DISCOVERY_PROTOCOL_VERSION_PS4;
	packet.user_credential = user_credential;

	ChiakiErrorCode err;
	if(discovery)
		err = chiaki_discovery_send(discovery, &packet, &addr, addr_len);
	else
	{
		ChiakiDiscovery tmp_discovery;
		err = chiaki_discovery_init(&tmp_discovery, log, AF_INET);
		if(err != CHIAKI_ERR_SUCCESS)
		{
			CHIAKI_LOGE(log, "Failed to init temporary discovery for wakeup: %s", chiaki_error_string(err));
			return err;
		}
		err = chiaki_discovery_send(&tmp_discovery, &packet, &addr, addr_len);
		chiaki_discovery_fini(&tmp_discovery);
	}

	return err;
}
