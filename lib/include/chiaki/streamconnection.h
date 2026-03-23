// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_STREAMCONNECTION_H
#define CHIAKI_STREAMCONNECTION_H

#include "feedbacksender.h"
#include "takion.h"
#include "log.h"
#include "ecdh.h"
#include "gkcrypt.h"
#include "audioreceiver.h"
#include "videoreceiver.h"
#include "congestioncontrol.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct chiaki_session_t ChiakiSession;

typedef enum chiaki_dualsense_effect_intensity_t
{
	Off = 0,
	Weak = 3,
	Medium = 2,
	Strong = 1,
} ChiakiDualSenseEffectIntensity;
typedef struct chiaki_stream_connection_t
{
	struct chiaki_session_t *session;
	ChiakiLog *log;
	ChiakiTakion takion;
	uint8_t *ecdh_secret;
	ChiakiGKCrypt *gkcrypt_local;
	ChiakiGKCrypt *gkcrypt_remote;
	uint8_t *streaminfo_early_buf;
	size_t streaminfo_early_buf_size;

	ChiakiPacketStats packet_stats;
	ChiakiAudioReceiver *audio_receiver;
	ChiakiVideoReceiver *video_receiver;
	ChiakiAudioReceiver *haptics_receiver;
	double packet_loss_max;
	uint8_t motion_counter[4];
	uint8_t led_state[3];
	uint8_t player_index;
	ChiakiDualSenseEffectIntensity haptic_intensity;
	ChiakiDualSenseEffectIntensity trigger_intensity;
	ChiakiFeedbackSender feedback_sender;
	ChiakiCongestionControl congestion_control;
	/**
	 * whether feedback_sender is initialized
	 * only if this is true, feedback_sender may be accessed!
	 */
	bool feedback_sender_active;
	/**
	 * protects feedback_sender and feedback_sender_active
	 */
	ChiakiMutex feedback_sender_mutex;

	/**
	 * signaled on change of state_finished or should_stop
	 */
	ChiakiCond state_cond;

	/**
	 * protects state, state_finished, state_failed and should_stop
	 */
	ChiakiMutex state_mutex;

	int state;
	bool state_finished;
	bool state_failed;
	bool should_stop;
	bool remote_disconnected;
	char *remote_disconnect_reason;

	double measured_bitrate;
} ChiakiStreamConnection;

CHIAKI_EXPORT ChiakiErrorCode chiaki_stream_connection_init(ChiakiStreamConnection *stream_connection, ChiakiSession *session, double packet_loss_max);
CHIAKI_EXPORT void chiaki_stream_connection_fini(ChiakiStreamConnection *stream_connection);

/**
 * Run stream_connection synchronously
 */
CHIAKI_EXPORT ChiakiErrorCode chiaki_stream_connection_run(ChiakiStreamConnection *stream_connection, chiaki_socket_t *socket);

CHIAKI_EXPORT ChiakiErrorCode stream_connection_send_toggle_mute_direct_message(ChiakiStreamConnection *stream_connection, bool muted);
/**
 * To be called from a thread other than the one chiaki_stream_connection_run() is running on to stop stream_connection
 */
CHIAKI_EXPORT ChiakiErrorCode chiaki_stream_connection_stop(ChiakiStreamConnection *stream_connection);

CHIAKI_EXPORT ChiakiErrorCode stream_connection_send_corrupt_frame(ChiakiStreamConnection *stream_connection, ChiakiSeqNum16 start, ChiakiSeqNum16 end);
CHIAKI_EXPORT ChiakiErrorCode stream_connection_send_idr_request(ChiakiStreamConnection *stream_connection);

#ifdef __cplusplus
}
#endif

#endif //CHIAKI_STREAMCONNECTION_H
