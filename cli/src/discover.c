// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki-cli.h>

#include <chiaki/discovery.h>

#include <argp.h>

#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <netinet/in.h>

static char doc[] = "Send a PS4 discovery request.";

#define ARG_KEY_HOST 'h'

static struct argp_option options[] = {
	{ "host", ARG_KEY_HOST, "Host", 0, "Host to send discovery request to", 0 },
	{ 0 }
};

typedef struct arguments
{
	const char *host;
} Arguments;

static int parse_opt(int key, char *arg, struct argp_state *state)
{
	Arguments *arguments = state->input;

	switch(key)
	{
		case ARG_KEY_HOST:
			arguments->host = arg;
			break;
		case ARGP_KEY_ARG:
			argp_usage(state);
			break;
		default:
			return ARGP_ERR_UNKNOWN;
	}

	return 0;
}

static struct argp argp = { options, parse_opt, 0, doc, 0, 0, 0 };

static void discovery_cb(ChiakiDiscoveryHost *host, void *user)
{
	ChiakiLog *log = user;

	CHIAKI_LOGI(log, "--");
	CHIAKI_LOGI(log, "Discovered Host:");
	CHIAKI_LOGI(log, "State:                             %s", chiaki_discovery_host_state_string(host->state));

	if(host->system_version)
		CHIAKI_LOGI(log, "System Version:                    %s", host->system_version);

	if(host->device_discovery_protocol_version)
		CHIAKI_LOGI(log, "Device Discovery Protocol Version: %s", host->device_discovery_protocol_version);

	if(host->host_request_port)
		CHIAKI_LOGI(log, "Request Port:                      %hu", (unsigned short)host->host_request_port);

	if(host->host_name)
		CHIAKI_LOGI(log, "Host Name:                         %s", host->host_name);

	if(host->host_type)
		CHIAKI_LOGI(log, "Host Type:                         %s", host->host_type);

	if(host->host_id)
		CHIAKI_LOGI(log, "Host ID:                           %s", host->host_id);

	if(host->running_app_titleid)
		CHIAKI_LOGI(log, "Running App Title ID:              %s", host->running_app_titleid);

	if(host->running_app_name)
		CHIAKI_LOGI(log, "Running App Name:                  %s%s", host->running_app_name, (strcmp(host->running_app_name, "Persona 5") == 0 ? " (best game ever)" : ""));

	CHIAKI_LOGI(log, "--");
}

CHIAKI_EXPORT int chiaki_cli_cmd_discover(ChiakiLog *log, int argc, char *argv[])
{
	Arguments arguments = { 0 };
	error_t argp_r = argp_parse(&argp, argc, argv, ARGP_IN_ORDER, NULL, &arguments);
	if(argp_r != 0)
		return 1;

	if(!arguments.host)
	{
		fprintf(stderr, "No host specified, see --help.\n");
		return 1;
	}

	ChiakiDiscovery discovery;
	ChiakiErrorCode err = chiaki_discovery_init(&discovery, log, AF_INET); // TODO: IPv6
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(log, "Discovery init failed");
		return 1;
	}

	ChiakiDiscoveryThread thread;
	err = chiaki_discovery_thread_start(&thread, &discovery, discovery_cb, NULL);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(log, "Discovery thread init failed");
		chiaki_discovery_fini(&discovery);
		return 1;
	}

	struct addrinfo *host_addrinfos;
	int r = getaddrinfo(arguments.host, NULL, NULL, &host_addrinfos);
	if(r != 0)
	{
		CHIAKI_LOGE(log, "getaddrinfo failed");
		return 1;
	}

	struct sockaddr *host_addr = NULL;
	socklen_t host_addr_len = 0;
	for(struct addrinfo *ai=host_addrinfos; ai; ai=ai->ai_next)
	{
		if(ai->ai_protocol != IPPROTO_UDP)
			continue;
		if(ai->ai_family != AF_INET) // TODO: IPv6
			continue;

		host_addr_len = ai->ai_addrlen;
		host_addr = (struct sockaddr *)malloc(host_addr_len);
		if(!host_addr)
			break;
		memcpy(host_addr, ai->ai_addr, host_addr_len);
	}
	freeaddrinfo(host_addrinfos);

	if(!host_addr)
	{
		CHIAKI_LOGE(log, "Failed to get addr for hostname");
		return 1;
	}

	ChiakiDiscoveryPacket packet;
	memset(&packet, 0, sizeof(packet));
	packet.cmd = CHIAKI_DISCOVERY_CMD_SRCH;
	packet.protocol_version = CHIAKI_DISCOVERY_PROTOCOL_VERSION_PS4;
	((struct sockaddr_in *)host_addr)->sin_port = htons(CHIAKI_DISCOVERY_PORT_PS4);
	err = chiaki_discovery_send(&discovery, &packet, host_addr, host_addr_len);
	if(err != CHIAKI_ERR_SUCCESS)
		CHIAKI_LOGE(log, "Failed to send discovery packet for PS4: %s", chiaki_error_string(err));
	packet.protocol_version = CHIAKI_DISCOVERY_PROTOCOL_VERSION_PS5;
	((struct sockaddr_in *)host_addr)->sin_port = htons(CHIAKI_DISCOVERY_PORT_PS5);
	err = chiaki_discovery_send(&discovery, &packet, host_addr, host_addr_len);
	if(err != CHIAKI_ERR_SUCCESS)
		CHIAKI_LOGE(log, "Failed to send discovery packet for PS5: %s", chiaki_error_string(err));

	while(1)
		sleep(1); // TODO: wtf

	return 0;
}
