// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <setsu.h>

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <signal.h>

Setsu *setsu;

#define WIDTH 1920
#define HEIGHT 942
#define TOUCHES_MAX 8
#define SCALE 16

struct {
	bool down;
	unsigned int tracking_id;
	unsigned int x, y;
} touches[TOUCHES_MAX];

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
	char buf[(1 + WIDTH/SCALE)*(HEIGHT/SCALE) + 1];
	size_t i = 0;
	for(size_t y=0; y<HEIGHT/SCALE; y++)
	{
		for(size_t x=0; x<WIDTH/SCALE; x++)
		{
			for(size_t t=0; t<TOUCHES_MAX; t++)
			{
				if(touches[t].down && touches[t].x / SCALE == x && touches[t].y / SCALE == y)
				{
					buf[i++] = 'X';
					goto beach;
				}
			}
			buf[i++] = '.';
beach:
			continue;
		}
		buf[i++] = '\n';
	}
	buf[i++] = '\0';
	assert(i == sizeof(buf));
	printf("\033[2J%s", buf);

	fflush(stdout);
}

void event(SetsuEvent *event, void *user)
{
	dirty = true;
	switch(event->type)
	{
		case SETSU_EVENT_DEVICE_ADDED: {
			if(event->dev_type != SETSU_DEVICE_TYPE_TOUCHPAD)
				break;
			SetsuDevice *dev = setsu_connect(setsu, event->path, SETSU_DEVICE_TYPE_TOUCHPAD);
			LOG("Device added: %s, connect %s\n", event->path, dev ? "succeeded" : "FAILED!");
			break;
		}
		case SETSU_EVENT_DEVICE_REMOVED:
			if(event->dev_type != SETSU_DEVICE_TYPE_TOUCHPAD)
				break;
			LOG("Device removed: %s\n", event->path);
			break;
		case SETSU_EVENT_TOUCH_DOWN:
			LOG("Down for %s, tracking id %d\n", setsu_device_get_path(event->dev), event->tracking_id);
			for(size_t i=0; i<TOUCHES_MAX; i++)
			{
				if(!touches[i].down)
				{
					touches[i].down = true;
					touches[i].tracking_id = event->tracking_id;
					break;
				}
			}
			break;
		case SETSU_EVENT_TOUCH_POSITION:
		case SETSU_EVENT_TOUCH_UP:
			if(event->type == SETSU_EVENT_TOUCH_UP)
				LOG("Up for %s, tracking id %d\n", setsu_device_get_path(event->dev), event->tracking_id);
			else
				LOG("Position for %s, tracking id %d: %u, %u\n", setsu_device_get_path(event->dev),
						event->tracking_id, (unsigned int)event->x, (unsigned int)event->y);
			for(size_t i=0; i<TOUCHES_MAX; i++)
			{
				if(touches[i].down && touches[i].tracking_id == event->tracking_id)
				{
					switch(event->type)
					{
						case SETSU_EVENT_TOUCH_POSITION:
							touches[i].x = event->x;
							touches[i].y = event->y;
							break;
						case SETSU_EVENT_TOUCH_UP:
							touches[i].down = false;
							break;
						default:
							break;
					}
				}
			}
			break;
		case SETSU_EVENT_BUTTON_DOWN:
		case SETSU_EVENT_BUTTON_UP:
			LOG("Button for %s: %llu %s\n", setsu_device_get_path(event->dev),
					(unsigned long long)event->button, event->type == SETSU_EVENT_BUTTON_DOWN ? "down" : "up");
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

	memset(touches, 0, sizeof(touches));
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

