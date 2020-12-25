// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki-cli.h>

#include <chiaki/discovery.h>

#include <argp.h>
#include <string.h>

static char doc[] = "Send a PS4 wakeup packet.";

#define ARG_KEY_HOST 'h'
#define ARG_KEY_REGISTKEY 'r'

static struct argp_option options[] = {
	{ "host", ARG_KEY_HOST, "Host", 0, "Host to send wakeup packet to", 0 },
	{ "registkey", ARG_KEY_REGISTKEY, "RegistKey", 0, "PS4 registration key", 0 },
	{ 0 }
};

typedef struct arguments
{
	const char *host;
	const char *registkey;
} Arguments;

static int parse_opt(int key, char *arg, struct argp_state *state)
{
	Arguments *arguments = state->input;

	switch(key)
	{
		case ARG_KEY_HOST:
			arguments->host = arg;
			break;
		case ARG_KEY_REGISTKEY:
			arguments->registkey = arg;
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

CHIAKI_EXPORT int chiaki_cli_cmd_wakeup(ChiakiLog *log, int argc, char *argv[])
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
	if(!arguments.registkey)
	{
		fprintf(stderr, "No registration key specified, see --help.\n");
		return 1;
	}
	if(strlen(arguments.registkey) > 8)
	{
		fprintf(stderr, "Given registkey is too long.\n");
		return 1;
	}

	uint64_t credential = (uint64_t)strtoull(arguments.registkey, NULL, 16);

	return chiaki_discovery_wakeup(log, NULL, arguments.host, credential, false);
}
