// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <setsu.h>

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <signal.h>

Setsu *setsu;

bool dirty = false;
bool log_mode;
volatile bool should_quit;

#define LOG(...) do { if(log_mode) fprintf(stderr, __VA_ARGS__); } while(0)

void sigint(int s)
{
	should_quit = true;
}

void print_state()
{
#if 0
	char buf[256];
	*buf = 0;
	printf("\033[2J%s", buf);
	fflush(stdout);
#endif
}

void event(SetsuEvent *event, void *user)
{
	dirty = true;
	switch(event->type)
	{
		case SETSU_EVENT_DEVICE_ADDED: {
			if(event->dev_type != SETSU_DEVICE_TYPE_MOTION)
				break;
			SetsuDevice *dev = setsu_connect(setsu, event->path, SETSU_DEVICE_TYPE_MOTION);
			LOG("Device added: %s, connect %s\n", event->path, dev ? "succeeded" : "FAILED!");
			break;
		}
		case SETSU_EVENT_DEVICE_REMOVED:
			if(event->dev_type != SETSU_DEVICE_TYPE_MOTION)
				break;
			LOG("Device removed: %s\n", event->path);
			break;
		// TODO: motion events
		default:
			break;
	}
}

void usage(const char *prog)
{
	printf("usage: %s [-l]\n  -l log mode\n", prog);
	exit(1);
}

int main(int argc, const char *argv[])
{
	log_mode = false;
	if(argc == 2)
	{
		if(!strcmp(argv[1], "-l"))
			log_mode = true;
		else
			usage(argv[0]);
	}
	else if(argc != 1)
		usage(argv[0]);

	setsu = setsu_new();
	if(!setsu)
	{
		printf("Failed to init setsu\n");
		return 1;
	}

	struct sigaction sa = {0};
	sa.sa_handler = sigint;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);

	dirty = true;
	while(!should_quit)
	{
		if(dirty && !log_mode)
			print_state();
		dirty = false;
		setsu_poll(setsu, event, NULL);
	}
	setsu_free(setsu);
	printf("\nさよなら!\n");
	return 0;
}

