#include <sdeck.h>
#include <fftw3.h>
#include <hidapi.h>
#include <math.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>

#define STEAMDECK_HAPTIC_INTERVAL 100000 // microseconds
#define PLAYTIME 5 // seconds

void calc_powers(fftw_complex * data, int N);
void max_power_freq(const int N, const double sampling_rate, double * frequency, double * freq_power, fftw_complex * power);
void zero_pad(double *data, int N);

void play_single_trackpad_rand(SDeck * sdeck)
{
	fprintf(stderr, "\n\nSINGLE TRACKPAD RANDOM HAPTIC DEMO\n---------------------------------------\n");
	fprintf(stderr, "Press ENTER to play demo\n");
	char enter = 0;
	while (enter != '\r' && enter != '\n') { enter = getchar(); }
	fprintf(stderr, "Playing random mono tunes now...\n\n");
    float haptic_interval_sec = (float)STEAMDECK_HAPTIC_INTERVAL / (float)1000000;
	time_t end = time(0) + PLAYTIME;
    srand((unsigned)end);
	while (time(0) < end)
	{
		double amp = (double)(rand() % 1000); // rand # from 0 to 999
		clock_t start2 = clock();
        uint16_t repeat = floor(haptic_interval_sec * amp);
		sdeck_haptic(sdeck, TRACKPAD_RIGHT, amp, STEAMDECK_HAPTIC_INTERVAL, repeat);
		clock_t end2 = clock();
        printf("Playing haptic with frequency of %f, repeat of %u with total period of %f seconds.\n", amp, repeat, haptic_interval_sec);
		printf("Took %f seconds\n", (((float)(end2-start2) / CLOCKS_PER_SEC)));
		usleep(STEAMDECK_HAPTIC_INTERVAL);
	}
}

void play_both_trackpads(SDeck * sdeck)
{
	fprintf(stderr, "\n\nDUAL TRACKPAD HAPTIC DEMO\n---------------------------\n");
	fprintf(stderr, "Press ENTER to play demo\n");
	char enter = 0;
	while (enter != '\r' && enter != '\n') { enter = getchar(); }
	fprintf(stderr, "Playing stereo tunes now...\n\n");
	double freql[12] = { 10, 20, 40, 80, 100, 110, 110, 110, 110, 110, 110, 110};
	double freqr[12] = { 20, 40, 80, 120, 160, 180, 200, 220, 240, 260, 280, 300};
	double repeatl[12] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
	double repeatr[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 11, 12};
	int playtimel = 0, playtimer = 0;

	for (int i = 0; i < 12; i++)
	{
		playtimel = sdeck_haptic(sdeck, TRACKPAD_RIGHT, freql[i], STEAMDECK_HAPTIC_INTERVAL, repeatl[i]);
		playtimer = sdeck_haptic(sdeck, TRACKPAD_RIGHT, freqr[i], STEAMDECK_HAPTIC_INTERVAL, repeatr[i]);
		printf("\ncurrent i %i\n left haptic - freq: %f, playtime: %i\n right haptic - freq: %f, playtime: %i\n", i, freql[i], playtimel, freqr[i], playtimer);
        if (playtimel > playtimer)
		    usleep(playtimel);
        else
            usleep(playtimer);
	}
}

void generate_signal(const int num_samples, const double sampling_rate, double signal_freq, double * data)
{
    for (int i = 0; i < num_samples; i++)
        data[i] = -25 * cos(signal_freq * 2.0f * M_PI * (double)i/(double)sampling_rate); //+ sin(signal_freq * 2.0f * M_PI * ((double)i/(double)sampling_rate));
}

void print_complex_array(fftw_complex * data, int N)
{
	printf("\nPrinting complex array values...\n");
	for (int i = 0; i < N; i++)
		printf("\nValue %i is: %f + %fi\n", i, data[i][0], data[i][1]);
}

void print_powers(fftw_complex * power, int N, float sampling_rate)
{
	printf("\nPrinting power of array...\n");
	const int originalN = 2 * N - 2;
	double sum = 0;
	for (int i = 0; i < N; i++)
	{
		const double power_calc = 2 * sqrt(power[i][0]) / (originalN);
		printf("\nFrequency %f has power: %f\n", i * sampling_rate / originalN, power_calc);
		sum += power_calc;
	}
	printf("\nTotal power is: %f\n", sum);
}

void print_autocorr(double * autocorr, int N, float sampling_rate)
{
	printf("\nPrinting autocorrelation values...\n");
	for (int i = 2; i < N/2 + 1; i++)
		printf("\nFrequency %f with autocorrelation: %f\n", (sampling_rate / i), (autocorr[i] / autocorr[0]));
}

void frequency_demo()
{
    fprintf(stderr, "\n\nFREQUENCY DETECTION DEMO\n---------------------------\n");
	fprintf(stderr, "Press ENTER to play demo\n");
	char enter = 0;
	while (enter != '\r' && enter != '\n') { enter = getchar(); }
	fprintf(stderr, "Detecting frequency now...\n\n");
	double sampling_rate = 3000; // samples per second
    double sampling_period = 50; // milliseconds
	const double max_freq = 1000; // maximum allowed haptic frequency for DualSense
    const double target_freq = 100;
	const int num_samples = sampling_rate * sampling_period / 1000.0;
	int realN = num_samples * 2; // zero padded array
	int complexN = num_samples + 1;
    double *hann, *pcm_data, frequency = 0, freq_power = 0;
    fftw_complex *freq_data;
    fftw_plan fft, ifft;
	hann = hann_init(num_samples);
	pcm_data = fftw_malloc(realN * sizeof(double));
	freq_data = fftw_malloc(complexN * sizeof(fftw_complex));

	fft = fftw_plan_dft_r2c_1d(realN, pcm_data, freq_data, FFTW_ESTIMATE);
	ifft = fftw_plan_dft_c2r_1d(complexN, freq_data, pcm_data, FFTW_ESTIMATE);

	generate_signal(num_samples, sampling_rate, target_freq, pcm_data);
	clock_t start = clock();
	hann_apply(pcm_data, hann, num_samples);
	zero_pad(pcm_data, num_samples);
    fftw_execute(fft);
	calc_powers(freq_data, complexN);
	max_power_freq(complexN, sampling_rate, &frequency, &freq_power, freq_data);
	clock_t end1 = clock();
	//print_powers(freq_data, complexN, sampling_rate);
    fftw_execute(ifft);
    clock_t end2 = clock();
    // Get autocoorelation of period using convolution thereorem (fft->powers->ifft)
    print_autocorr(pcm_data, complexN, sampling_rate);
    printf("\n\nFREQUENCY RESULTS\n-------------------\n");
    printf("Power Calculated Frequency is: %f Hz with power: %f\n", frequency, freq_power);
    if (compute_freq(pcm_data, complexN, sampling_rate, &frequency, max_freq) == 0)
        printf("Autocorrelation Calculated Frequency is: %f Hz\n", frequency);
    printf("Real Frquency of signal is: %f Hz\n", target_freq);
	printf("\n\nFinding frequency via max power frequency took %f seconds\n", (((float)(end1-start) / CLOCKS_PER_SEC)));
    printf("Finding frquency via autocorrelation took %f seconds\n", (((float)(end2-start) / CLOCKS_PER_SEC)));
	
    fftw_destroy_plan(fft);
    fftw_destroy_plan(ifft);

	fftw_free(hann);
    fftw_free(pcm_data);
	fftw_free(freq_data);
	fftw_cleanup();
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
    play_single_trackpad_rand(sdeck);
    play_both_trackpads(sdeck);
    sdeck_free(sdeck);
    frequency_demo();
	return 0;
}