// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki-cli.h>

#include <argp.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static const char doc[] =
	"CLI for Chiaki (PlayStation Remote Play Client)"
	"\v"
	"Supported commands are:\n"
	"  discover    Discover Consoles.\n"
	"  wakeup      Send Wakeup Packet.\n";

#define ARG_KEY_VERBOSE 'v'

static struct argp_option options[] = {
	{ "verbose", ARG_KEY_VERBOSE, NULL, 0, "Verbose Logging", 0 },
	{ 0 }
};

typedef struct context
{
	ChiakiLog log;
} Context;

static int call_subcmd(struct argp_state *state, const char *name, int (*subcmd)(ChiakiLog *log, int argc, char *argv[]))
{
	if(state->next < 1 || state->argc < state->next)
		return 1;

	int argc = state->argc - state->next + 1;
	char **argv = &state->argv[state->next - 1];

	size_t l = strlen(state->name) + strlen(name) + 2;
	argv[0] = malloc(l);
	if(!argv[0])
		return 1;
	snprintf(argv[0], l, "%s %s", state->name, name);

	int r = subcmd(state->input, argc, argv);

	free(argv[0]);
	return r;
}

static int parse_opt(int key, char *arg, struct argp_state *state)
{
	Context *ctx = state->input;

	switch(key)
	{
		case ARG_KEY_VERBOSE:
			ctx->log.level_mask = CHIAKI_LOG_ALL;
			break;
		case ARGP_KEY_ARG:
			if(strcmp(arg, "discover") == 0)
				exit(call_subcmd(state, "discover", chiaki_cli_cmd_discover));
			else if(strcmp(arg, "wakeup") == 0)
				exit(call_subcmd(state, "wakeup", chiaki_cli_cmd_wakeup));
			// fallthrough
		case ARGP_KEY_END:
			argp_usage(state);
			break;
		default:
			return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

static struct argp argp = { options, parse_opt, "<cmd> [CMD-ARGS...]", doc, 0, 0, 0 };

int main(int argc, char *argv[])
{
	Context ctx;
	chiaki_log_init(&ctx.log, CHIAKI_LOG_ALL & ~CHIAKI_LOG_VERBOSE, chiaki_log_cb_print, NULL);

	argp_parse(&argp, argc, argv, ARGP_IN_ORDER, NULL, &ctx);

	return 0;
}

