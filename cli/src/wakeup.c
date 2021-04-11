// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki-cli.h>

#include <chiaki/discovery.h>

#include <argp.h>
#include <string.h>

static char doc[] = "Send a PS4 wakeup packet.";

#define ARG_KEY_HOST 'h'
#define ARG_KEY_REGISTKEY 'r'
#define ARG_KEY_PS4 '4'
#define ARG_KEY_PS5 '5'

static struct argp_option options[] = {
	{ "host", ARG_KEY_HOST, "Host", 0, "Host to send wakeup packet to", 0 },
	{ "registkey", ARG_KEY_REGISTKEY, "RegistKey", 0, "Remote Play registration key (plaintext)", 0 },
	{ "ps4", ARG_KEY_PS4, NULL, 0, "PlayStation 4", 0 },
	{ "ps5", ARG_KEY_PS5, NULL, 0, "PlayStation 5 (default)", 0 },
	{ 0 }
};

typedef struct arguments
{
	const char *host;
	const char *registkey;
	bool ps5;
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
		case ARG_KEY_PS4:
			arguments->ps5 = false;
			break;
		case ARG_KEY_PS5:
			arguments->ps5 = true;
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
	arguments.ps5 = true;
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

	return chiaki_discovery_wakeup(log, NULL, arguments.host, credential, arguments.ps5);
}
