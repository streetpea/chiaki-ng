// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_LAUNCHSPEC_H
#define CHIAKI_LAUNCHSPEC_H

#include "common.h"

#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct chiaki_launch_spec_t
{
	ChiakiTarget target;
	unsigned int mtu;
	unsigned int rtt;
	uint8_t *handshake_key;
	unsigned int width;
	unsigned int height;
	unsigned int max_fps;
	ChiakiCodec codec;
	unsigned int bw_kbps_sent;
} ChiakiLaunchSpec;

CHIAKI_EXPORT int chiaki_launchspec_format(char *buf, size_t buf_size, ChiakiLaunchSpec *launch_spec);

#ifdef __cplusplus
}
#endif

#endif // CHIAKI_LAUNCHSPEC_H
