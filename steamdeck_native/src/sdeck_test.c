// precondition: create a <build dir> somewhere on the filesystem (preferably outside of the HIDAPI source)
// this is the place where all intermediate/build files are going to be located
// cd <build dir>
// configure the build
// cmake <HIDAPI source dir>
// build it!
// cmake --build .
// install command now would install things into /usr
// rm -rf build && mkdir build && cd build && cmake .. -DHIDAPI_BUILD_HIDTEST=TRUE -DHIDAPI_WITH_HIDRAW=TRUE && cmake --build .
// export PKG_CONFIG_PATH=/app/lib/pkgconfig
//HIDAPI_WITH_HIDRAW TUE
#include <sdeck.h>
#include <hidapi.h>
#include <math.h>
#include <fftw3.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#define STEAMDECK_UPDATE_INTERVAL_MS 4
#define STEAMDECK_HAPTIC_INTERVAL 20000 // microseconds
#define STEAM_CONTROLLER_MAGIC_PERIOD_RATIO 495483.0f
#define ENDTIME_MS 10000
#define PLAYNUM 1000

void max_power_freq(const int N, const double sampling_rate, double * frequency, fftw_complex * power);
void calc_powers(fftw_complex * data, int N);
void max_power_freq(const int N, const double sampling_rate, double * frequency, fftw_complex * power);
void zero_pad(double *data, int N);
hid_device * is_steam_deck();
struct sdeck_t
{
	// Steam Deck only one device and doesn't hotplug (you're either using i)
	hid_device *hiddev;
	SDeckMotion prev_motion;
	bool motion_dirty;
};
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
}
/*
void haptic_generator(SDeck * sdeck)
{
	float frequency = 10;
	uint16_t position, amplitude, period, count = 0;
	period = STEAM_CONTROLLER_MAGIC_PERIOD_RATIO * (1.0f / 3000);
	count = 1;
	amplitude = 1;
	send_haptic(sdeck, position, amplitude, period, count);
	position = 1;
	send_haptic(sdeck, position, amplitude, period, count);
}*/
// int main()
// {
// 	SDeck * sdeck = NULL;
// 	sdeck = sdeck_new();
// 	if(!sdeck)
// 	{
// 		printf("Failed to init sdeck\n");
// 		return 1;
// 	}
// 	//double freq[9] = { 10, 110, 210, 310, 410, 510, 610, 710, 810 };
// 	//float freq[6] = {50, 65, 85, 105, 135, 100};
// 	float freq[6] = {50, 55, 50, 55, 50, 55};
// 	while(true)
// 	{
// 		for (int i = 0; i < 6; i++)
// 		{

// 			printf("\ncurrent i %f\n", freq[i]);
// 			sdeck_haptic(sdeck, freq[i], freq[i], STEAMDECK_HAPTIC_INTERVAL);
// 			usleep(STEAMDECK_HAPTIC_INTERVAL);
// 		}
// 	}

// /* 	
// 	clock_t start = clock();
// 	for (int i = 0; i < PLAYNUM; i++)
// 	{
// 		uint16_t amp = rand();
// 		clock_t start2 = clock();
// 		sdeck_haptic(sdeck, amp, amp, STEAMDECK_HAPTIC_INTERVAL);
// 		clock_t end2 = clock();
// 		printf("Took %f seconds\n", (((float)(end2-start2) / CLOCKS_PER_SEC)));
// 		// note: values only change if you move the device (otherwise events aren't sent)
// 		usleep(STEAMDECK_HAPTIC_INTERVAL);
// 		//clock_t start = clock();
// 		//sdeck_read(sdeck, print_motion, NULL);
// 		//clock_t end = clock();
// 		//printf("Took %f seconds\n", (((float)(end-start) / CLOCKS_PER_SEC)));
// 	}
// 	clock_t end = clock();
// 	*/
// 	//printf("Took %f seconds\n", (((float)(end-start) / CLOCKS_PER_SEC)));

// 	//sdeck_haptic(sdeck, 1, 40000, STEAM_CONTROLLER_MAGIC_PERIOD_RATIO / (10 * (double)STEAMDECK_UPDATE_INTERVAL_MS));
// 	//send_haptic(sdeck, , 10, 5000, 1);
// 	//sdeck_haptic(sdeck, 20000, 20000, STEAMDECK_HAPTIC_INTERVAL);
	
// 	sdeck_free(sdeck);
// 	return 0;
// }

void generate_signal(const int num_samples, const double sampling_rate, double signal_freq, double * data)
{
    for (int i = 0; i < num_samples; i++)
        data[i] = cos(signal_freq * 2.0f * M_PI * (double)i/(double)sampling_rate); //+ sin(signal_freq * 2.0f * M_PI * ((double)i/(double)sampling_rate));
}

void print_complex_array(fftw_complex * data, int N)
{
	printf("\nPrinting complex array values...\n");
	for (int i = 0; i < N; i++)
		printf("\nValue %i is: %f + %fi\n", i, data[i][0], data[i][1]);
}

void print_powers(fftw_complex * power, int N)
{
	printf("\nPrinting power of array...\n");
	for (int i = 0; i < N; i++)
		printf("\nValue %i is: %f\n", i, power[i][0] / (2 * N));
}

void print_autocorr(double * autocorr, int N, float sampling_rate)
{
	printf("\nPrinting autocorrelation values...\n");
	for (int i = 2; i < N/2 + 1; i++)
		printf("\nFrequency %f is: %f\n", (sampling_rate / i), (autocorr[i] / autocorr[0]));
}

int main()
{
	double sampling_rate = 12000; // samples per second
    double sampling_period = 100; // milliseconds
	const double max_freq = 1000; // maximum allowed haptic frequency for DualSense
	const int num_samples = sampling_rate * sampling_period / 1000.0;
	int realN = num_samples * 2; // zero padded array
	int complexN = num_samples + 1;
    double *hann, *pcm_data, frequency = 0;
    fftw_complex *freq_data;
    fftw_plan fft, ifft;
	hann = hann_init(num_samples);
	pcm_data = fftw_malloc(realN * sizeof(double));
	freq_data = fftw_malloc(complexN * sizeof(fftw_complex));
	//clock_t start = clock();
	// Take autocoorelation of period using convolution thereorem (fft->powers->ifft)
	fft = fftw_plan_dft_r2c_1d(realN, pcm_data, freq_data, FFTW_ESTIMATE);
	ifft = fftw_plan_dft_c2r_1d(complexN, freq_data, pcm_data, FFTW_ESTIMATE);

	//memset(data, N, 0);
	generate_signal(num_samples, sampling_rate, 100, pcm_data);
	clock_t start = clock();
	hann_apply(pcm_data, hann, num_samples);
	zero_pad(pcm_data, num_samples);
    fftw_execute(fft);
	calc_powers(freq_data, complexN);
	max_power_freq(complexN, sampling_rate, &frequency, freq_data);
	clock_t end2 = clock();
	//printf("Took %f seconds\n", (((float)(end1-start) / CLOCKS_PER_SEC)));
	printf("\nPower Frequency is %f Hz\n", frequency);
	//print_powers(freq_data, complexN);
    fftw_execute(ifft);
	print_autocorr(pcm_data, complexN, sampling_rate);
	if (compute_freq(pcm_data, complexN, sampling_rate, &frequency, max_freq) == 0)
		printf("\nFrequency is %f Hz\n", frequency);
	printf("Took %f seconds\n", (((float)(end2-start) / CLOCKS_PER_SEC)));
	
    fftw_destroy_plan(fft);
    fftw_destroy_plan(ifft);

	fftw_free(hann);
    fftw_free(pcm_data);
	fftw_free(freq_data);
	fftw_cleanup();
    return 0;
}
