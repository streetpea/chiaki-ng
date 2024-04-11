// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef _SDECK_H
#define _SDECK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

typedef struct sdeck_t SDeck;

double * hann_init(int N);
void hann_apply(double *data, double *han, int N);
int compute_freq(double *data, int N, const double sampling_rate, double * frequency, double max_freq);

enum SDHapticPos
{
    TRACKPAD_RIGHT = 0,
    TRACKPAD_LEFT = 1,
};

typedef struct SDControls
{
    uint32_t ptype;          //0x00-0x03
    uint32_t seq;            //0x04-0x07 
    uint32_t buttons1;       //0x08-0x0B Note: Buttons themselves are interspersed among these 2 uint32_t structures
    uint32_t buttons2;       //0x0C-0x0F Since we don't need them for our purposes, I haven't included those structures in this file
    int16_t lpad_x;          //0x10-0x11
    int16_t lpad_y;          //0x12-0x13
    int16_t rpad_x;          //0x14-0x15
    int16_t rpad_y;          //0x16-0x17
    int16_t accel_x;         //0x18-0x19 side to side of deck
    int16_t accel_z;         //0x1A-0x1B top to bottom of deck
    int16_t accel_y;         //0x1C-0x1D front to back of deck
    int16_t gyro_x;          //0x1E-0x1F pitch forward (+)/back (-)
    int16_t gyro_z;          //0x20-0x21 roll counter-clockwise twist (+)
    int16_t gyro_y;          //0x22-0x23 yaw roate screen side-to-side counter-clockwise (+)(when screen up)
    int16_t orient_w;        //0x24-0x25
    int16_t orient_x;        //0x26-0x27
    int16_t orient_z;        //0x28-0x29
    int16_t orient_y;        //0x2A-0x2B
    int16_t ltrig;           //0x2C-0x2D
    int16_t rtrig;           //0x2E-0x2F
    int16_t lthumb_x;        //0x30-0x31
    int16_t lthumb_y;        //0x32-0x33
    int16_t rthumb_x;        //0x34-0x35
    int16_t rthumb_y;        //0x36-0x37
    int16_t lpad_pressure;   //0x38-0x39
    int16_t rpad_pressure;	 //0x3A-0x3B
    int16_t leftStickTouch;  //0x3C-0x3D
    int16_t rightStickTouch; //0x3E-0x3F
} SDControls;

// Guaranteed tightly packed because it can be broken into 4 byte (32 bit word size) 
// and 8 byte (64 bit word size) chunks evenly in all places and the biggest element is 4 bytes
// => don't need __attribute__((packed)) or special mapping in this specific instance
// limitation: only supports little endian, would need to add bitswap for big-endin 
// but don't here since no target platforms are big-endian

typedef struct SDHaptic
{
    uint8_t len; //  = 7 (length of haptic data 1 + 2 + 2 + 2)
    uint8_t position; // (right trackpad = 0, left trackpad = 1)
    uint16_t period_high; // amount of time signal is high during total period in microseconds
    uint16_t period_low; // amount of time signal is low during total period in microseconds
    uint16_t repeat_count; // amount of time above period total ms (low + high) is repeated
} SDHaptic;

// Guaranteed tightly packed because it can be broken into 4 byte (32 bit word size) 
// and 8 byte (64 bit word size) chunks evenly in all places and the biggest element is 4 bytes
// => don't need __attribute__((packed)) or special mapping in this specific instance
// limitation: only supports little endian, would need to add bitswap for big-endin 
// but don't here since no target platforms are big-endian

// currently only events for motion 
// Additional events could be added based on the controller data but aren't needed
// since that functionality is already provided w/ QT/SDL
typedef enum {
	/* Event will have motion set (accel, gyro + orientation) */
	SDECK_EVENT_MOTION,
    /* Event will have no data */
    SDECK_EVENT_GYRO_ENABLE
} SDeckEventType;

typedef struct sdeck_motion_t
{
    // values normalized from initial readings
    float accel_x, accel_y, accel_z; // unit is 1G
    float gyro_x, gyro_y, gyro_z; // unit is rad/sec
    float orient_w, orient_x, orient_y, orient_z; // orientation sent by Deck
} SDeckMotion;

typedef struct sdeck_event_t
{
	SDeckEventType type;
    union {
        SDeckMotion motion;
        bool enabled;
    };
} SDeckEvent;

typedef void (*SDeckEventCb)(SDeckEvent *event, void *user);

SDeck *sdeck_new();
void sdeck_free(SDeck *sdeck);
void sdeck_read(SDeck *sdeck, SDeckEventCb cb, void *user);
int sdeck_haptic(SDeck *sdeck, uint8_t position, double frequency, uint32_t interval, const uint16_t repeat);
int sdeck_haptic_ratio(SDeck *sdeck, uint8_t position, double frequency, uint32_t interval, double ratio, const uint16_t repeat);
int send_haptic(SDeck* sdeck, uint8_t position, uint16_t period_high, uint16_t period_low, uint16_t repeat_count);
int sdeck_haptic_init(SDeck * sdeck, int samples);
int play_pcm_haptic(SDeck *sdeck, uint8_t position, int16_t *buf, const int32_t num_elements, const int sampling_rate);

#ifdef __cplusplus
}
#endif

#endif