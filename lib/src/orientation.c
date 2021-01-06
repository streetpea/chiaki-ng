// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/orientation.h>

CHIAKI_EXPORT void chiaki_orientation_init(ChiakiOrientation *orient)
{
	orient->x = orient->y = orient->z = 0.0f;
	orient->w = 1.0f;
}

#define BETA 0.1f		// 2 * proportional gain
static float inv_sqrt(float x);

CHIAKI_EXPORT void chiaki_orientation_update(ChiakiOrientation *orient,
		float gx, float gy, float gz, float ax, float ay, float az, float time_step_sec)
{
	float q0 = orient->w, q1 = orient->x, q2 = orient->y, q3 = orient->z;
	// Madgwick's IMU algorithm.
	// See: http://www.x-io.co.uk/node/8#open_source_ahrs_and_imu_algorithms
	float recip_norm;
	float s0, s1, s2, s3;
	float q_dot1, q_dot2, q_dot3, q_dot4;
	float _2q0, _2q1, _2q2, _2q3, _4q0, _4q1, _4q2 ,_8q1, _8q2, q0q0, q1q1, q2q2, q3q3;

	// Rate of change of quaternion from gyroscope
	q_dot1 = 0.5f * (-q1 * gx - q2 * gy - q3 * gz);
	q_dot2 = 0.5f * (q0 * gx + q2 * gz - q3 * gy);
	q_dot3 = 0.5f * (q0 * gy - q1 * gz + q3 * gx);
	q_dot4 = 0.5f * (q0 * gz + q1 * gy - q2 * gx);

	// Compute feedback only if accelerometer measurement valid (avoids NaN in accelerometer normalisation)
	if(!((ax == 0.0f) && (ay == 0.0f) && (az == 0.0f)))
	{
		// Normalise accelerometer measurement
		recip_norm = inv_sqrt(ax * ax + ay * ay + az * az);
		ax *= recip_norm;
		ay *= recip_norm;
		az *= recip_norm;

		// Auxiliary variables to avoid repeated arithmetic
		_2q0 = 2.0f * q0;
		_2q1 = 2.0f * q1;
		_2q2 = 2.0f * q2;
		_2q3 = 2.0f * q3;
		_4q0 = 4.0f * q0;
		_4q1 = 4.0f * q1;
		_4q2 = 4.0f * q2;
		_8q1 = 8.0f * q1;
		_8q2 = 8.0f * q2;
		q0q0 = q0 * q0;
		q1q1 = q1 * q1;
		q2q2 = q2 * q2;
		q3q3 = q3 * q3;

		// Gradient decent algorithm corrective step
		s0 = _4q0 * q2q2 + _2q2 * ax + _4q0 * q1q1 - _2q1 * ay;
		s1 = _4q1 * q3q3 - _2q3 * ax + 4.0f * q0q0 * q1 - _2q0 * ay - _4q1 + _8q1 * q1q1 + _8q1 * q2q2 + _4q1 * az;
		s2 = 4.0f * q0q0 * q2 + _2q0 * ax + _4q2 * q3q3 - _2q3 * ay - _4q2 + _8q2 * q1q1 + _8q2 * q2q2 + _4q2 * az;
		s3 = 4.0f * q1q1 * q3 - _2q1 * ax + 4.0f * q2q2 * q3 - _2q2 * ay;
		recip_norm = inv_sqrt(s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3); // normalise step magnitude
		s0 *= recip_norm;
		s1 *= recip_norm;
		s2 *= recip_norm;
		s3 *= recip_norm;

		// Apply feedback step
		q_dot1 -= BETA * s0;
		q_dot2 -= BETA * s1;
		q_dot3 -= BETA * s2;
		q_dot4 -= BETA * s3;
	}

	// Integrate rate of change of quaternion to yield quaternion
	q0 += q_dot1 * time_step_sec;
	q1 += q_dot2 * time_step_sec;
	q2 += q_dot3 * time_step_sec;
	q3 += q_dot4 * time_step_sec;

	// Normalise quaternion
	recip_norm = inv_sqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
	q0 *= recip_norm;
	q1 *= recip_norm;
	q2 *= recip_norm;
	q3 *= recip_norm;

	orient->x = q1;
	orient->y = q2;
	orient->z = q3;
	orient->w = q0;
}

static float inv_sqrt(float x)
{
	// Fast inverse square-root
	// See: http://en.wikipedia.org/wiki/Fast_inverse_square_root
	float halfx = 0.5f * x;
	float y = x;
	long i = *(long*)&y;
	i = 0x5f3759df - (i>>1);
	y = *(float*)&i;
	y = y * (1.5f - (halfx * y * y));
	return y;
}

CHIAKI_EXPORT void chiaki_orientation_tracker_init(ChiakiOrientationTracker *tracker)
{
	tracker->accel_x = 0.0f;
	tracker->accel_y = 1.0f;
	tracker->accel_z = 0.0f;
	tracker->gyro_x = tracker->gyro_y = tracker->gyro_z = 0.0f;
	chiaki_orientation_init(&tracker->orient);
	tracker->timestamp = 0;
	tracker->first_sample = true;
}

CHIAKI_EXPORT void chiaki_orientation_tracker_update(ChiakiOrientationTracker *tracker,
		float gx, float gy, float gz, float ax, float ay, float az, uint32_t timestamp_us)
{
	tracker->gyro_x = gx;
	tracker->gyro_y = gy;
	tracker->gyro_z = gz;
	tracker->accel_x = ax;
	tracker->accel_y = ay;
	tracker->accel_z = az;
	if(tracker->first_sample)
	{
		tracker->first_sample = false;
		tracker->timestamp = timestamp_us;
		return;
	}
	uint64_t delta_us = timestamp_us;
	if(delta_us < tracker->timestamp)
		delta_us += (1ULL << 32);
	delta_us -= tracker->timestamp;
	tracker->timestamp = timestamp_us;
	chiaki_orientation_update(&tracker->orient, gx, gy, gz, ax, ay, az, (float)delta_us / 1000000.0f);
}

CHIAKI_EXPORT void chiaki_orientation_tracker_apply_to_controller_state(ChiakiOrientationTracker *tracker,
		ChiakiControllerState *state)
{
	state->gyro_x = tracker->gyro_x;
	state->gyro_y = tracker->gyro_y;
	state->gyro_z = tracker->gyro_z;
	state->accel_x = tracker->accel_x;
	state->accel_y = tracker->accel_y;
	state->accel_z = tracker->accel_z;
	state->orient_x = tracker->orient.x;
	state->orient_y = tracker->orient.y;
	state->orient_z = tracker->orient.z;
	state->orient_w = tracker->orient.w;
}
