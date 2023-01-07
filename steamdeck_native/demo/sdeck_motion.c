// 1. Test in terminal with ./sdeck-demo-motion
// 2. Test to file with ./sdeck-demo-motion > gyro_log.log
// 	  After ctl+c read log with less -r gyro_log.log

#include <sdeck.h>
#include <hidapi.h>
#include <math.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#define STEAMDECK_UPDATE_INTERVAL_MS 4

void print_motion(SDeckEvent * event, void *user)
{
	printf("\nGyro x is %f rad/s\n", event->motion.gyro_x);
	printf("\nGyro y is %f rad/s\n", event->motion.gyro_y);
	printf("\nGyro z is %f rad/s\n", event->motion.gyro_z);
	printf("\nAccel x is %f g\n", event->motion.accel_x);
	printf("\nAccel y is %f g\n", event->motion.accel_y);
	printf("\nAccel z is %f g\n", event->motion.accel_z);
	printf("\nOrient w is %f\n", event->motion.orient_w);
	printf("\nOrient x is %f\n", event->motion.orient_x);
	printf("\nOrient y is %f\n", event->motion.orient_y);
	printf("\nOrient z is %f\n", event->motion.orient_z);
	time_t current = time(NULL);
	// Gives general idea of when in local time different events happened when scrolling through log
	printf("Current local time is: %s\n\n",asctime(localtime(&current)));
}

int main()
{
	SDeck * sdeck = NULL;
	sdeck = sdeck_new();
	if(!sdeck)
	{
		printf("Failed to init sdeck\n");
		return 1;
	}
	
	fprintf(stderr, "MOTION DEMO\n--------------\n");
	fprintf(stderr, "Prints motion values, updating in real-time.\n");
	fprintf(stderr, "Values only change if you move the device (otherwise events aren't sent)\n");
	fprintf(stderr, "Press ENTER to begin demo and ctrl+c to end demo\n");
	char enter = 0;
	while (enter != '\r' && enter != '\n') { enter = getchar(); }
	fprintf(stderr, "Printing motion change events now...\n\n");

	while(1)
	{
		sdeck_read(sdeck, print_motion, NULL);
		usleep(STEAMDECK_UPDATE_INTERVAL_MS * 1000);
	}
	sdeck_free(sdeck);
	return 0;
}
