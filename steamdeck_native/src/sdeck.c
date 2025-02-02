#include <sdeck.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>
#include <stdlib.h>
#include <hidapi.h>
#include <math.h>
#include <unistd.h>
#include <fftw3.h>
#define ENABLE_LOG

#ifdef ENABLE_LOG
#define SDECK_LOG(...) fprintf(stderr, __VA_ARGS__)
#endif

#define DEG2RAD (2.0f * M_PI / 360.0f)

#define STEAM_DECK_ACCEL_RES 16384.0f
#define STEAM_DECK_GYRO_RES 16.0f
#define STEAM_DECK_ORIENT_RES 32492.0f
// multiplier bc heavier Steam Deck => lower accel values
#define ACCEL_MOVESPEED_MULT 1.5f
// sqrt(qw^2 + qx^2 + qy^2 qz^2) approx. above constant due to normalization
#define STEAM_DECK_ORIENT_FUZZ 24.0f
#define STEAM_DECK_ACCEL_FUZZ 12.0f
#define FUZZ_FILTER_PREV_WEIGHT 0.75f
#define FUZZ_FILTER_PREV_WEIGHT2x 0.6f
#define STEAM_DECK_GYRO_DEADZONE 24.0f
// amount of cycles of 0 motion data to wait before sending enable motion message
// time = cycles * 4ms (i.e., 1000 * 4ms = 4000 ms = 4s)
#define STEAM_DECK_MOTION_COOLDOWN 1000
#define STEAM_DECK_HAPTIC_COMMAND 0x8f
#define STEAM_DECK_HAPTIC_LENGTH 0x07
#define STEAM_DECK_HAPTIC_INTENSITY 0.38f
#define STEAM_DECK_CUTOFF_FREQ 250.0f
#define STEAM_DECK_HAPTIC_SAMPLING_FREQ 3000.0f

typedef struct freq_t
{
	int N;
	fftw_plan fft;
	double *hann, *butterworth, *pcm_data;
	fftw_complex *freq_data;
} FreqFinder;

struct sdeck_t
{
	// Steam Deck only one device and doesn't hotplug (you're either using i)
	hid_device *hiddev;
	SDeckMotion prev_motion;
	int gyro;
	bool motion_dirty;
	FreqFinder *freqfinder;
};

hid_device *is_steam_deck();
void movemult_accel(float *accel, float mult);
void calc_powers(fftw_complex *data, int N);
void max_power_freq(const int N, const double sampling_rate, double *frequency, double *freq_power, fftw_complex *power);
void generate_event(SDeck *sdeck, SDeckEventType type, SDeckEventCb cb, void *user);
double * butterworth_init();
void lpf_apply(double * pcm_data, double * butterworth, int N);
FreqFinder *freqfinder_new(int samples);

SDeck *sdeck_new()
{
	SDeck *sdeck = calloc(1, sizeof(SDeck));
	if (!sdeck)
		return NULL;

	if (hid_init())
	{
		free(sdeck);
		SDECK_LOG("Could not initialize hidapi...Steam Deck native gyro & haptics offline\n");
		return NULL;
	}
	sdeck->hiddev = is_steam_deck();
	if (sdeck->hiddev == NULL)
	{
		hid_exit();
		free(sdeck);
		SDECK_LOG("Could not connect to Steam Deck...Steam Deck native gyro & haptics offline\n");
		return NULL;
	}
	memset(&sdeck->prev_motion, 0, sizeof(SDeckMotion));
	sdeck->motion_dirty = false;
	sdeck->gyro = STEAM_DECK_MOTION_COOLDOWN;
	sdeck->freqfinder = NULL;
	return sdeck;
}

int sdeck_haptic_init(SDeck *sdeck, int samples)
{
	sdeck->freqfinder = freqfinder_new(samples);
	if (!sdeck->freqfinder)
		return -1;
	return 0;
}

FreqFinder *freqfinder_new(int samples)
{
	FreqFinder *freqfinder = fftw_malloc(sizeof(FreqFinder));
	freqfinder->N = samples;
	freqfinder->hann = hann_init(samples);
	freqfinder->butterworth = butterworth_init();
	freqfinder->pcm_data = fftw_malloc(2 * samples * sizeof(double));
	freqfinder->freq_data = fftw_malloc((samples + 1) * sizeof(fftw_complex));
	freqfinder->fft = fftw_plan_dft_r2c_1d(2 * samples, freqfinder->pcm_data, freqfinder->freq_data, FFTW_MEASURE);
	return freqfinder;
}

void haptic_free(FreqFinder *freqfinder)
{
	fftw_destroy_plan(freqfinder->fft);

	fftw_free(freqfinder->hann);
	fftw_free(freqfinder->butterworth);
	fftw_free(freqfinder->pcm_data);
	fftw_free(freqfinder->freq_data);
	fftw_free(freqfinder);

	fftw_cleanup();
}

void sdeck_free(SDeck *sdeck)
{
	if (!sdeck)
		return;
	hid_close(sdeck->hiddev);
	hid_exit();
	if (sdeck->freqfinder)
		haptic_free(sdeck->freqfinder);
	free(sdeck);
}

void print_device(struct hid_device_info *cur_dev)
{
	printf("Device Found\n  type: %04hx %04hx\n  path: %s\n  serial_number: %ls", cur_dev->vendor_id, cur_dev->product_id, cur_dev->path, cur_dev->serial_number);
	printf("\n");
	printf("  Manufacturer: %ls\n", cur_dev->manufacturer_string);
	printf("  Product:      %ls\n", cur_dev->product_string);
	printf("  Release:      %hx\n", cur_dev->release_number);
	printf("  Interface:    %d\n", cur_dev->interface_number);
	printf("  Usage (page): 0x%hx (0x%hx)\n", cur_dev->usage, cur_dev->usage_page);
	printf("\n");
}

void print_devices(struct hid_device_info *cur_dev)
{
	while (cur_dev)
	{
		print_device(cur_dev);
		cur_dev = cur_dev->next;
	}
}

hid_device *is_steam_deck()
{

#define MAX_STR 255
	uint16_t vid = 0x28DE;
	uint16_t pid = 0x1205;
	hid_device *handle;
	struct hid_device_info *devs = NULL, *cur_dev = NULL;
	uint16_t usage_page = 0xFFFF;				// usagePage for SteamDeck controls/haptics
	uint16_t usage = 0x0001;					// usage for SteamDeck controls/haptics (general)
	wchar_t serial_wstr[MAX_STR / 4] = {L'\0'}; // serial number string rto search for, if any
	char devpath[MAX_STR] = {0};				// path to open, if filter by usage

	devs = hid_enumerate(vid, pid);
	cur_dev = devs;
	while (cur_dev)
	{
		if ((!vid || cur_dev->vendor_id == vid) &&
			(!pid || cur_dev->product_id == pid) &&
			(!usage_page || cur_dev->usage_page == usage_page) &&
			(!usage || cur_dev->usage == usage) &&
			(serial_wstr[0] == L'\0' || wcscmp(cur_dev->serial_number, serial_wstr) == 0))
		{
			strncpy(devpath, cur_dev->path, MAX_STR); // save it!
			print_device(cur_dev);
		}
		cur_dev = cur_dev->next;
	}
#undef MAX_STR
	if(devs)
		hid_free_enumeration(devs);

	if (devpath[0])
	{
		handle = hid_open_path(devpath);
		if (!handle)
		{
			SDECK_LOG("Error: Failed to open Steam Deck device at %s\n", devpath);
			return NULL;
		}
	}
	else
	{
		// Steam Deck not found
		return NULL;
	}
	return handle;
}

void print_steam_deck_controls(SDControls *sdc)
{
	printf("\nSteam Deck acceleration x: %d\n", sdc->accel_x);
	printf("Steam Deck acceleration y: %d\n", sdc->accel_y);
	printf("Steam Deck acceleration z: %d\n", sdc->accel_z);
	printf("Steam Deck gyro x (pitch): %d\n", sdc->gyro_x);
	printf("Steam Deck gyro y (yaw): %d\n", sdc->gyro_y);
	printf("Steam Deck gyro z (roll): %d\n", sdc->gyro_z);
	printf("Steam Deck orientation w: %d\n", sdc->orient_w);
	printf("Steam Deck orientation x: %d\n", sdc->orient_x);
	printf("Steam Deck orientation y: %d\n", sdc->orient_y);
	printf("Steam Deck orientation z: %d\n", sdc->orient_z);
	printf("\n");
}

double *hann_init(int N) // calc hann coefficients (once per array size and can then be reused)
{
	double *han = fftw_malloc(N * sizeof(double));
	for (int i = 0; i < N; i++)
		han[i] = 0.5 * (1 - cos(2 * M_PI * i / (N - 1)));
	return han;
}
double *butterworth_init()
{
	double *butterworth = fftw_malloc(5 * sizeof(double));
	const double ff = STEAM_DECK_CUTOFF_FREQ / STEAM_DECK_HAPTIC_SAMPLING_FREQ;
	const double ita = 1.0/tan(M_PI * ff);
	const double q = sqrt(2.0);
	butterworth[0] = 1.0 / (1.0 + q*ita + ita*ita);
	butterworth[1] = 2 * butterworth[0];
	butterworth[2] = butterworth[0];
	butterworth[3] = 2.0 * (ita * ita - 1.0) * butterworth[0];
	butterworth[4] = -(1.0 - q * ita + ita * ita) * butterworth[0];
	return butterworth;
}

void lpf_apply(double *buf, double * butterworth, int buf_count)
{
	double holder_buffer[buf_count];
	holder_buffer[0] = buf[0];
	holder_buffer[1] = buf[1];
	for (int i = 2; i < buf_count; i++)
		holder_buffer[i] = butterworth[0] * buf[i] + butterworth[1] * buf[i - 1] + butterworth[2] * buf[i - 2] + butterworth[3] * holder_buffer[i - 1] + butterworth[4] * holder_buffer[i - 2];
	for (int i = 2; i < buf_count; i++)
	{
		buf[i] = holder_buffer[i];
	}
}

void hann_apply(double *data, double *han, int N) // apply hann window to data
{
	for (int i = 0; i < N; i++)
		data[i] = data[i] * han[i];
}

void zero_pad(double *data, int N)
{
	for (int i = N; i < 2 * N; i++)
		data[i] = 0;
}

void calc_powers(fftw_complex *data, int N)
{
	// multiply each element by complex conjugate => real^2 + imaginary^2
	for (int i = 0; i < N; i++) // round N/2 up
	{
		data[i][0] = data[i][0] * data[i][0] + data[i][1] * data[i][1]; // real component
		data[i][1] = 0;													// imaginary component
	}
}

int compute_freq(double *data, int N, const double sampling_rate, double *frequency, double max_freq)
{
	// lowest freq we can detect is sampling_rate / 2 => period of 2 / sampling_rate
	int max_pos = 2;
	for (int i = 3; i < N/2 + 1; i++)
	{
		if (data[i] > data[max_pos])
			max_pos = i;
	}
	// check for next harmonic masquerading as fundamental
	if ((max_pos < N/4 + 1) && (data[max_pos * 2] > 0.8 * data[max_pos]))
		max_pos *= 2;
	double period = (double)max_pos / (double)sampling_rate;
	double temp_freq = (1 / period);
	if (temp_freq > max_freq)
		return -1;
	*frequency = temp_freq;
	return 0;
}

void max_power_freq(const int N, const double sampling_rate, double *frequency, double *freq_power, fftw_complex *power)
{
	int max_pos = 0;
	for (int i = 1; i < N; i++)
	{
		if (power[i][0] > power[max_pos][0])
			max_pos = i;
	}
	if (max_pos == 0)
		return;
	const int originalN = 2 * N - 2;
	*frequency = ((max_pos * sampling_rate) / originalN);
	*freq_power = ((2 * sqrt(power[max_pos][0])) / originalN);
}

void calc_freqs(FreqFinder *freqfinder, const int sampling_rate, double *freq, double *freq_power)
{
	int N = freqfinder->N;
	int complexN = freqfinder->N + 1;
	lpf_apply(freqfinder->pcm_data, freqfinder->butterworth, N);
	hann_apply(freqfinder->pcm_data, freqfinder->hann, N);
	zero_pad(freqfinder->pcm_data, N);
	fftw_execute(freqfinder->fft);
	calc_powers(freqfinder->freq_data, complexN);
	max_power_freq(complexN, sampling_rate, freq, freq_power, freqfinder->freq_data);
}

int get_data(FreqFinder *freqfinder, int16_t *buf, const int num_elements)
{
	if (freqfinder->N != num_elements)
	{
		SDECK_LOG("\nBuffer size mismatch...initialized buffer is not right size!\n");
		return -1;
	}
	for (int i = 0; i < num_elements; i++)
	{
		freqfinder->pcm_data[i] = buf[i];
	}
	return 0;
}

void find_repeat(double * data, const int num_samples, const double frequency, const int sampling_rate, const double avg_min, double * total_avg, int * repeat_count)
{
	int counter = 0;
	int interval_length = sampling_rate / frequency;
	double avg = 0;
	const int repeat_max = 20;
	*total_avg = 0;
	*repeat_count = 0;
	// printf("\ninterval length is %i, num_samples is: %i\n", interval_length, num_samples);
	for (int i = 0; i < num_samples; i++)
	{
		avg += fabs(data[i]);
		if ((i + 1) % interval_length == 0)
		{
			avg /= interval_length;
			if (avg > avg_min)
			{
				(*repeat_count)++;
				(*total_avg) += avg;
			}
			avg = 0; 
		}
	}
	if ((*repeat_count) == 0)
	{
		int leftover = num_samples % interval_length;
		if (leftover)
			avg /= leftover;
		if(avg < avg_min)
			return;
		(*repeat_count) = 1;
		(*total_avg) = avg;
	}
	(*total_avg) /= (*repeat_count);
	(*repeat_count) = ((*repeat_count) > repeat_max) ? repeat_max : (*repeat_count); 
}

int play_pcm_haptic(SDeck *sdeck, uint8_t position, int16_t *buf, const int num_elements, const int sampling_rate)
{
	uint32_t interval = 0;
	const int avg_min = 50; // don't play samples that are less than 1% volume
	int repeat = 0;
	int32_t playtime = 0;
	double freq = 0, avg = 0, ratio = 0, freq_power = 0;
	if (get_data(sdeck->freqfinder, buf, num_elements))
		return -1;
	// interval in microseconds
	interval = 1000000 * ((double)num_elements / (double)sampling_rate);
	calc_freqs(sdeck->freqfinder, sampling_rate, &freq, &freq_power);
	if (!freq)
		return 0;
	avg = 5 * freq_power;
	if (avg < avg_min)
		return 0;
	repeat = (STEAM_DECK_HAPTIC_INTENSITY * num_elements * freq) / (double)sampling_rate;
	repeat = (repeat > 1) ? repeat : 1;
	playtime = sdeck_haptic_ratio(sdeck, position, freq, interval, 0.2, repeat);
	if (playtime < 0)
		return playtime;
	if (playtime < interval)
		return 1;
	return 2;
}

int send_haptic(SDeck *sdeck, uint8_t position, uint16_t period_high, uint16_t period_low, uint16_t repeat_count)
{
	hid_device *handle = sdeck->hiddev;
	unsigned char haptic[65];
	SDHaptic sdh;
	memset(haptic, 0, sizeof(haptic));
	sdh.len = STEAM_DECK_HAPTIC_LENGTH;
	sdh.position = position;
	sdh.period_high = period_high;
	sdh.period_low = period_low;
	sdh.repeat_count = repeat_count;
	haptic[0] = 0x00; // report ID
	haptic[1] = STEAM_DECK_HAPTIC_COMMAND;
	memcpy(haptic + 2, &sdh, sizeof(sdh));
	int res = 0;
	res = hid_write(handle, haptic, 65);
	if (res < 0)
	{
		SDECK_LOG("Unable to write() haptic: %ls\n", hid_error(handle));
		return -1;
	}
	return 0;
}
// period in microseconds, frequency in Hz
int sdeck_haptic(SDeck *sdeck, uint8_t position, double frequency, uint32_t interval, const uint16_t repeat)
{
	int res = 0;
	double freq_min = 500000.0 / (double)interval; // can play haptic for at most 2 intervals w/ borrow from future
	uint16_t period = 0;
	if (frequency >= freq_min)
	{
		period = 500000 / frequency; // 1,000,000 microseconds in sec at 50% duty cycle (50 % period high and 50% period low) 
		res = send_haptic(sdeck, position, period, period, repeat);
		if (res < 0)
			return res;
	}
	// return interval played
	return period * 2 * repeat;
}

int sdeck_haptic_ratio(SDeck *sdeck, uint8_t position, double frequency, uint32_t interval, double ratio, const uint16_t repeat)
{
	int res = 0;
	double ratio_high = 0, ratio_low = 0;
	ratio_high = 2 * ratio;
	ratio_low = 2 - ratio_high;
	double freq_min = 500000.0 / (double)interval; // can play haptic for at most 2 intervals w/ borrow from future
	uint16_t period = 0;
	if (frequency >= freq_min)
	{
		period = 500000 / frequency; // 1,000,000 microseconds in sec at 50% duty cycle (50 % period high and 50% period low) 
		res = send_haptic(sdeck, position, ceil(ratio_high * period), ceil(ratio_low * period), repeat);
		if (res < 0)
			return res;
	}
	// return interval played
	return period * 2 * repeat;
}

int enable_gyro(SDeck *sdeck)
{
	hid_device *handle = sdeck->hiddev;
	unsigned char enable[65] = {0x00, 0x87, 0x0f, 0x30, 0x18, 0x00, 0x07, 0x07, 0x00, 0x08, 0x07, 0x00, 0x31, 0x02, 0x00, 0x18, 0x00
    , 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    , 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    , 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	int res = 0;
	res = hid_write(handle, enable, 65);
	if (res < 0)
	{
		SDECK_LOG("Unable to write() enable gyro: %ls\n", hid_error(handle));
		return -1;
	}
	return 0;
}

// input fuzz filter like the one used in kernel input system
void fuzz(const float cur, float *prev, const float fuzz, const float wprev, const float wprev2x, bool *motion_dirty)
{
	// smooth (0-1) is weight of previous (0.9 = 90%) to use at fuzz value
	// wprev2x (0-1) is weight of previous (0.5 = 50%) to use at 2x fuzz value
	if ((cur < *prev + fuzz / 2) && (cur > *prev - fuzz / 2))
		return;
	if ((cur < *prev + fuzz) && (cur > *prev - fuzz))
	{
		*prev = (wprev * *prev + (1 - wprev) * cur);
		*motion_dirty = true;
		return;
	}
	if ((cur < *prev + fuzz * 2) && (cur > *prev - fuzz * 2))
	{
		*motion_dirty = true;
		*prev = (wprev2x * *prev + (1 - wprev2x) * cur);
		return;
	}
	*motion_dirty = true;
	*prev = cur;
}
// input deadzone filter (flat value) in kernel input system
void deadzone(const float cur, float *prev, const float deadzone, bool *motion_dirty)
{
	float next_prev = 0;
	if ((cur < deadzone) && (cur > -deadzone))
		next_prev = 0;
	else if ((cur < deadzone * 2) && (cur > -deadzone * 2))
		next_prev = (cur / 4);
	else if ((cur < deadzone * 4) && (cur > -deadzone * 4))
		next_prev = (cur / 2);
	else
		next_prev = cur;
	if (*prev != next_prev)
	{
		*motion_dirty = true;
		*prev = next_prev;
	}
}

void movemult_accel(float *accel, float mult)
{
	*accel = *accel * mult;
}

void sdeck_read(SDeck *sdeck, SDeckEventCb cb, void *user)
{
	int res = 0;
	hid_device *handle = sdeck->hiddev;
	unsigned char buf[64];
	SDControls sdc;
	memset(buf, 0, sizeof(buf));
	memset(&sdc, 0, sizeof(SDControls));
	bool data_to_read = false;

	if (!handle)
	{
		SDECK_LOG("Steam Deck not found\n");
		return;
	}
	// Set the hid_read() function to be non-blocking (returns immediately with values or 0).
	hid_set_nonblocking(handle, 1);
	// read until no more data to read in buffer or error, keeping only most recent data (minimize updates for stream performance)
	while (true)
	{
		res = hid_read(handle, buf, (sizeof(buf)));
		if (res < 0)
		{
			SDECK_LOG("Unable to read(): %ls\n", hid_error(handle));
			break;
		}
		// if res == 0 => no more data to read
		if (res == 0)
			break;
		if (res > 0)
		{
			memcpy(&sdc, buf, sizeof(buf));
			data_to_read = true;
			continue;
		}
	}
	// if data to read, read and apply filters
	// apply fuzz filter to accel and orient to remove noise
	// apply deadzone filter to gyro to reduce jitter when still
	if (data_to_read)
	{
		if(sdc.accel_x == 0 && sdc.accel_y == 0 && sdc.accel_z == 0 && sdc.orient_w == 0 && sdc.orient_x == 0 && sdc.orient_y == 0 && 
			sdc.orient_z == 0 && sdc.gyro_x == 0 && sdc.gyro_y == 0 && sdc.gyro_z == 0)
		{
			sdeck->gyro--;
			if(sdeck->gyro <= 0)
			{
				if(enable_gyro(sdeck) == 0)
					sdeck->gyro = STEAM_DECK_MOTION_COOLDOWN;
				generate_event(sdeck, SDECK_EVENT_GYRO_ENABLE, cb, user);
			}
		}
		else
		{
			float accel_x = sdc.accel_x;
			float accel_y = sdc.accel_y;
			float accel_z = sdc.accel_z;
			movemult_accel(&accel_x, ACCEL_MOVESPEED_MULT);
			movemult_accel(&accel_y, ACCEL_MOVESPEED_MULT);
			movemult_accel(&accel_z, ACCEL_MOVESPEED_MULT);
			fuzz(accel_x, &sdeck->prev_motion.accel_x, STEAM_DECK_ACCEL_FUZZ, FUZZ_FILTER_PREV_WEIGHT, FUZZ_FILTER_PREV_WEIGHT2x, &sdeck->motion_dirty);
			fuzz(accel_y, &sdeck->prev_motion.accel_y, STEAM_DECK_ACCEL_FUZZ, FUZZ_FILTER_PREV_WEIGHT, FUZZ_FILTER_PREV_WEIGHT2x, &sdeck->motion_dirty);
			fuzz(accel_z, &sdeck->prev_motion.accel_z, STEAM_DECK_ACCEL_FUZZ, FUZZ_FILTER_PREV_WEIGHT, FUZZ_FILTER_PREV_WEIGHT2x, &sdeck->motion_dirty);
			fuzz(sdc.orient_w, &sdeck->prev_motion.orient_w, STEAM_DECK_ORIENT_FUZZ, FUZZ_FILTER_PREV_WEIGHT, FUZZ_FILTER_PREV_WEIGHT2x, &sdeck->motion_dirty);
			fuzz(sdc.orient_x, &sdeck->prev_motion.orient_x, STEAM_DECK_ORIENT_FUZZ, FUZZ_FILTER_PREV_WEIGHT, FUZZ_FILTER_PREV_WEIGHT2x, &sdeck->motion_dirty);
			fuzz(sdc.orient_y, &sdeck->prev_motion.orient_y, STEAM_DECK_ORIENT_FUZZ, FUZZ_FILTER_PREV_WEIGHT, FUZZ_FILTER_PREV_WEIGHT2x, &sdeck->motion_dirty);
			fuzz(sdc.orient_z, &sdeck->prev_motion.orient_z, STEAM_DECK_ORIENT_FUZZ, FUZZ_FILTER_PREV_WEIGHT, FUZZ_FILTER_PREV_WEIGHT2x, &sdeck->motion_dirty);
			deadzone(sdc.gyro_x, &sdeck->prev_motion.gyro_x, STEAM_DECK_GYRO_DEADZONE, &sdeck->motion_dirty);
			deadzone(sdc.gyro_y, &sdeck->prev_motion.gyro_y, STEAM_DECK_GYRO_DEADZONE, &sdeck->motion_dirty);
			deadzone(sdc.gyro_z, &sdeck->prev_motion.gyro_z, STEAM_DECK_GYRO_DEADZONE, &sdeck->motion_dirty);
			// send events for data we want to send (currently just motion data)
			generate_event(sdeck, SDECK_EVENT_MOTION, cb, user);
		}
	}
}

void generate_event(SDeck *sdeck, SDeckEventType type, SDeckEventCb cb, void *user)
{

	SDeckEvent event;

#define BEGIN_EVENT(tp)                   \
	do                                    \
	{                                     \
		memset(&event, 0, sizeof(event)); \
		event.type = tp;                  \
	} while (0)
#define SEND_EVENT()      \
	do                    \
	{                     \
		cb(&event, user); \
	} while (0)

	if (type == SDECK_EVENT_MOTION)
	{
		// create event if change occurred
		if (sdeck->motion_dirty)
		{
			BEGIN_EVENT(SDECK_EVENT_MOTION);
			event.motion.accel_x = sdeck->prev_motion.accel_x / STEAM_DECK_ACCEL_RES;
			event.motion.accel_y = sdeck->prev_motion.accel_y / STEAM_DECK_ACCEL_RES;
			event.motion.accel_z = -sdeck->prev_motion.accel_z / STEAM_DECK_ACCEL_RES;
			event.motion.gyro_x = DEG2RAD * sdeck->prev_motion.gyro_x / STEAM_DECK_GYRO_RES;
			event.motion.gyro_y = DEG2RAD * sdeck->prev_motion.gyro_y / STEAM_DECK_GYRO_RES;
			event.motion.gyro_z = DEG2RAD * -sdeck->prev_motion.gyro_z / STEAM_DECK_GYRO_RES;
			event.motion.orient_w = sdeck->prev_motion.orient_w / STEAM_DECK_ORIENT_RES;
			event.motion.orient_x = sdeck->prev_motion.orient_x / STEAM_DECK_ORIENT_RES;
			event.motion.orient_y = sdeck->prev_motion.orient_y / STEAM_DECK_ORIENT_RES;
			event.motion.orient_z = -sdeck->prev_motion.orient_z / STEAM_DECK_ORIENT_RES;
			SEND_EVENT();
		}
		sdeck->motion_dirty = false;
	}
	if (type == SDECK_EVENT_GYRO_ENABLE)
	{
		BEGIN_EVENT(SDECK_EVENT_GYRO_ENABLE);
			if(sdeck->gyro > 0)
				event.enabled = true;
			else
				event.enabled = false;
		SEND_EVENT();
	}

#undef BEGIN_EVENT
#undef SEND_EVENT
}