// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/launchspec.h>
#include <chiaki/session.h>
#include <chiaki/base64.h>

#include <stdio.h>

static const char launchspec_fmt[] =
	"{"
		"\"sessionId\":\"sessionId4321\","
		"\"streamResolutions\":["
			"{"
				"\"resolution\":"
				"{"
					"\"width\":%u," // 0
					"\"height\":%u" // 1
				"},"
				"\"maxFps\":%u," // 2
				"\"score\":10"
			"}"
		"],"
		"\"network\":{"
			"\"bwKbpsSent\":%u," // 3
			"\"bwLoss\":0.001000,"
			"\"mtu\":%u," // 4
			"\"rtt\":%u," // 5
			"\"ports\":[53,2053]"
		"},"
		"\"slotId\":1,"
		"\"appSpecification\":{"
			"\"minFps\":30,"
			"\"minBandwidth\":0,"
			"\"extTitleId\":\"ps3\","
			"\"version\":1,"
			"\"timeLimit\":1,"
			"\"startTimeout\":100,"
			"\"afkTimeout\":100,"
			"\"afkTimeoutDisconnect\":100"
		"},"
		"\"konan\":{"
			"\"ps3AccessToken\":\"accessToken\","
			"\"ps3RefreshToken\":\"refreshToken\""
		"},\"requestGameSpecification\":{"
			"\"model\":\"bravia_tv\","
			"\"platform\":\"android\","
			"\"audioChannels\":\"5.1\","
			"\"language\":\"sp\","
			"\"acceptButton\":\"X\","
			"\"connectedControllers\":[\"xinput\",\"ds3\",\"ds4\"],"
			"\"yuvCoefficient\":\"bt601\","
			"\"videoEncoderProfile\":\"hw4.1\","
			"\"audioEncoderProfile\":\"audio1\"" // 6
			"%s"
		"},"
		"\"userProfile\":{"
			"\"onlineId\":\"psnId\","
			"\"npId\":\"npId\","
			"\"region\":\"US\","
			"\"languagesUsed\":[\"en\",\"jp\"]"
		"},"
		"%s" // 7
		"%s" // 8
		"\"handshakeKey\":\"%s\"" // 9
	"}";

CHIAKI_EXPORT int chiaki_launchspec_format(char *buf, size_t buf_size, ChiakiLaunchSpec *launch_spec)
{
	char handshake_key_b64[CHIAKI_HANDSHAKE_KEY_SIZE * 2];
	ChiakiErrorCode err = chiaki_base64_encode(launch_spec->handshake_key, CHIAKI_HANDSHAKE_KEY_SIZE, handshake_key_b64, sizeof(handshake_key_b64));
	if(err != CHIAKI_ERR_SUCCESS)
		return -1;

	char *extras[3];
	if(chiaki_target_is_ps5(launch_spec->target)) // TODO: probably also for ps4, but only 12
	{
		extras[0] = ",\"adaptiveStreamMode\": \"resize\"";
		extras[1] = chiaki_codec_is_h265(launch_spec->codec)
			? "\"videoCodec\":\"hevc\","
			: "\"videoCodec\":\"avc\",";
		extras[2] = chiaki_codec_is_hdr(launch_spec->codec)
			? "\"dynamicRange\":\"HDR\","
			: "\"dynamicRange\":\"SDR\",";
	}
	else
		extras[0] = extras[1] = extras[2] = "";

	int written = snprintf(buf, buf_size, launchspec_fmt,
			launch_spec->width, launch_spec->height, launch_spec->max_fps,
			launch_spec->bw_kbps_sent, launch_spec->mtu, launch_spec->rtt,
			extras[0], extras[1], extras[2], handshake_key_b64);
	if(written < 0 || written >= buf_size)
		return -1;
	return written;
}
