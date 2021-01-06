// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <setsu.h>

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <signal.h>
#include <math.h>

Setsu *setsu;

#define NAME_LEN 8
const char * const names[] = {
	"accel x ",
	"accel y ",
	"accel z ",
	" gyro x ",
	" gyro y ",
	" gyro z "
};
union
{
	struct
	{
		float accel_x, accel_y, accel_z;
		float gyro_x, gyro_y, gyro_z;
	};
	float v[6];
} vals;
uint32_t timestamp;
bool dirty = false;
bool log_mode;
volatile bool should_quit;

#define LOG(...) do { if(log_mode) fprintf(stderr, __VA_ARGS__); } while(0)

void sigint(int s)
{
	should_quit = true;
}

#define BAR_LENGTH 100
#define BAR_MAX 2.0f
#define BAR_MAX_GYRO 180.0f

void print_state()
{
	char buf[6 * (1 + NAME_LEN + BAR_LENGTH) + 1];
	size_t i = 0;
	for(size_t b=0; b<6; b++)
	{
		buf[i++] = '\n';
		memcpy(buf + i, names[b], NAME_LEN);
		i += NAME_LEN;
		buf[i++] = '[';
		size_t max = BAR_LENGTH-2;
		for(size_t bi=0; bi<max; bi++)
		{
#define BAR_VAL(x) ((b > 2 ? BAR_MAX_GYRO : BAR_MAX) * (2.0f * (float)(x) / (float)max - 1.0f))
			float cur = BAR_VAL(bi);
			float prev = BAR_VAL((int)bi - 1);
			if(prev < 0.0f && cur >= 0.0f)
			{
				buf[i++] = '|';
				continue;
			}
			bool cov = ((vals.v[b] < 0.0f) == (cur < 0.0f)) && fabsf(vals.v[b]) > fabsf(cur);
			float next = BAR_VAL(bi + 1);
#define MARK_VAL (b > 2 ? 90.0f : 1.0f)
			bool mark = cur < -MARK_VAL && next >= -MARK_VAL || prev < MARK_VAL && cur >= MARK_VAL;
			buf[i++] = cov ? (mark ? '#' : '=') : (mark ? '.' : ' ');
#undef BAR_VAL
		}
		buf[i++] = ']';
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
		case SETSU_EVENT_MOTION:
			LOG("Motion: %f, %f, %f / %f, %f, %f / %u\n",
					event->motion.accel_x, event->motion.accel_y, event->motion.accel_z,
					event->motion.gyro_x, event->motion.gyro_y, event->motion.gyro_z,
					(unsigned int)event->motion.timestamp);
			vals.accel_x = event->motion.accel_x;
			vals.accel_y = event->motion.accel_y;
			vals.accel_z = event->motion.accel_z;
			vals.gyro_x = event->motion.gyro_x;
			vals.gyro_y = event->motion.gyro_y;
			vals.gyro_z = event->motion.gyro_z;
			timestamp = event->motion.timestamp;
			dirty = true;
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

