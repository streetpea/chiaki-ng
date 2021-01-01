// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL


#include "chiaki/common.h"
#include <chiaki/streamconnection.h>
#include <chiaki/session.h>
#include <chiaki/launchspec.h>
#include <chiaki/base64.h>
#include <chiaki/audio.h>
#include <chiaki/video.h>

#include <string.h>
#include <assert.h>
#ifndef _WIN32
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#endif

#include <takion.pb.h>
#include <pb_encode.h>
#include <pb_decode.h>
#include <pb.h>

#include "utils.h"
#include "pb_utils.h"


#define STREAM_CONNECTION_PORT 9296

#define EXPECT_TIMEOUT_MS 5000

#define HEARTBEAT_INTERVAL_MS 1000


typedef enum {
	STATE_IDLE,
	STATE_TAKION_CONNECT,
	STATE_EXPECT_BANG,
	STATE_EXPECT_STREAMINFO
} StreamConnectionState;

void chiaki_session_send_event(ChiakiSession *session, ChiakiEvent *event);

static void stream_connection_takion_cb(ChiakiTakionEvent *event, void *user);
static void stream_connection_takion_data(ChiakiStreamConnection *stream_connection, ChiakiTakionMessageDataType data_type, uint8_t *buf, size_t buf_size);
static void stream_connection_takion_data_protobuf(ChiakiStreamConnection *stream_connection, uint8_t *buf, size_t buf_size);
static void stream_connection_takion_data_rumble(ChiakiStreamConnection *stream_connection, uint8_t *buf, size_t buf_size);
static ChiakiErrorCode stream_connection_send_big(ChiakiStreamConnection *stream_connection);
static ChiakiErrorCode stream_connection_send_disconnect(ChiakiStreamConnection *stream_connection);
static void stream_connection_takion_data_idle(ChiakiStreamConnection *stream_connection, uint8_t *buf, size_t buf_size);
static void stream_connection_takion_data_expect_bang(ChiakiStreamConnection *stream_connection, uint8_t *buf, size_t buf_size);
static void stream_connection_takion_data_expect_streaminfo(ChiakiStreamConnection *stream_connection, uint8_t *buf, size_t buf_size);
static ChiakiErrorCode stream_connection_send_streaminfo_ack(ChiakiStreamConnection *stream_connection);
static void stream_connection_takion_av(ChiakiStreamConnection *stream_connection, ChiakiTakionAVPacket *packet);
static ChiakiErrorCode stream_connection_send_heartbeat(ChiakiStreamConnection *stream_connection);

CHIAKI_EXPORT ChiakiErrorCode chiaki_stream_connection_init(ChiakiStreamConnection *stream_connection, ChiakiSession *session)
{
	stream_connection->session = session;
	stream_connection->log = session->log;

	stream_connection->ecdh_secret = NULL;
	stream_connection->gkcrypt_remote = NULL;
	stream_connection->gkcrypt_local = NULL;

	ChiakiErrorCode err = chiaki_mutex_init(&stream_connection->state_mutex, false);
	if(err != CHIAKI_ERR_SUCCESS)
		goto error;

	err = chiaki_cond_init(&stream_connection->state_cond);
	if(err != CHIAKI_ERR_SUCCESS)
		goto error_state_mutex;

	err = chiaki_packet_stats_init(&stream_connection->packet_stats);
	if(err != CHIAKI_ERR_SUCCESS)
		goto error_state_cond;

	stream_connection->video_receiver = NULL;
	stream_connection->audio_receiver = NULL;

	err = chiaki_mutex_init(&stream_connection->feedback_sender_mutex, false);
	if(err != CHIAKI_ERR_SUCCESS)
		goto error_packet_stats;

	stream_connection->state = STATE_IDLE;
	stream_connection->state_finished = false;
	stream_connection->state_failed = false;
	stream_connection->should_stop = false;
	stream_connection->remote_disconnected = false;
	stream_connection->remote_disconnect_reason = NULL;

	return CHIAKI_ERR_SUCCESS;

error_packet_stats:
	chiaki_packet_stats_fini(&stream_connection->packet_stats);
error_state_cond:
	chiaki_cond_fini(&stream_connection->state_cond);
error_state_mutex:
	chiaki_mutex_fini(&stream_connection->state_mutex);
error:
	return err;
}

CHIAKI_EXPORT void chiaki_stream_connection_fini(ChiakiStreamConnection *stream_connection)
{
	free(stream_connection->remote_disconnect_reason);

	chiaki_gkcrypt_free(stream_connection->gkcrypt_remote);
	chiaki_gkcrypt_free(stream_connection->gkcrypt_local);

	free(stream_connection->ecdh_secret);

	chiaki_packet_stats_fini(&stream_connection->packet_stats);

	chiaki_mutex_fini(&stream_connection->feedback_sender_mutex);

	chiaki_cond_fini(&stream_connection->state_cond);
	chiaki_mutex_fini(&stream_connection->state_mutex);
}

static bool state_finished_cond_check(void *user)
{
	ChiakiStreamConnection *stream_connection = user;
	return stream_connection->state_finished || stream_connection->should_stop || stream_connection->remote_disconnected;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_stream_connection_run(ChiakiStreamConnection *stream_connection)
{
	ChiakiSession *session = stream_connection->session;
	ChiakiErrorCode err;

	ChiakiTakionConnectInfo takion_info;
	takion_info.log = stream_connection->log;
	takion_info.sa_len = session->connect_info.host_addrinfo_selected->ai_addrlen;
	takion_info.sa = malloc(takion_info.sa_len);
	if(!takion_info.sa)
		return CHIAKI_ERR_MEMORY;
	memcpy(takion_info.sa, session->connect_info.host_addrinfo_selected->ai_addr, takion_info.sa_len);
	err = set_port(takion_info.sa, htons(STREAM_CONNECTION_PORT));
	assert(err == CHIAKI_ERR_SUCCESS);
	takion_info.ip_dontfrag = false;

	takion_info.enable_crypt = true;
	takion_info.protocol_version = chiaki_target_is_ps5(session->target) ? 12 : 9;

	takion_info.cb = stream_connection_takion_cb;
	takion_info.cb_user = stream_connection;

	err = chiaki_mutex_lock(&stream_connection->state_mutex);
	assert(err == CHIAKI_ERR_SUCCESS);

#define CHECK_STOP(quit_label) do { \
	if(stream_connection->should_stop) \
	{ \
		goto quit_label; \
	} } while(0)

	stream_connection->audio_receiver = chiaki_audio_receiver_new(session, &stream_connection->packet_stats);
	if(!stream_connection->audio_receiver)
	{
		CHIAKI_LOGE(session->log, "StreamConnection failed to initialize Audio Receiver");
		return CHIAKI_ERR_UNKNOWN;
	}

	stream_connection->video_receiver = chiaki_video_receiver_new(session, &stream_connection->packet_stats);
	if(!stream_connection->video_receiver)
	{
		CHIAKI_LOGE(session->log, "StreamConnection failed to initialize Video Receiver");
		err = CHIAKI_ERR_UNKNOWN;
		goto err_audio_receiver;
	}

	stream_connection->state = STATE_TAKION_CONNECT;
	stream_connection->state_finished = false;
	stream_connection->state_failed = false;
	err = chiaki_takion_connect(&stream_connection->takion, &takion_info);
	free(takion_info.sa);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(session->log, "StreamConnection connect failed");
		chiaki_mutex_unlock(&stream_connection->state_mutex);
		goto err_video_receiver;
	}

	ChiakiCongestionControl congestion_control;
	err = chiaki_congestion_control_start(&congestion_control, &stream_connection->takion, &stream_connection->packet_stats);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(session->log, "StreamConnection failed to start Congestion Control");
		goto close_takion;
	}

	err = chiaki_cond_timedwait_pred(&stream_connection->state_cond, &stream_connection->state_mutex, EXPECT_TIMEOUT_MS, state_finished_cond_check, stream_connection);
	assert(err == CHIAKI_ERR_SUCCESS || err == CHIAKI_ERR_TIMEOUT);
	CHECK_STOP(close_takion);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(session->log, "StreamConnection Takion connect failed");
		goto err_congestion_control;
	}

	CHIAKI_LOGI(session->log, "StreamConnection sending big");

	stream_connection->state = STATE_EXPECT_BANG;
	stream_connection->state_finished = false;
	stream_connection->state_failed = false;
	err = stream_connection_send_big(stream_connection);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(session->log, "StreamConnection failed to send big");
		goto disconnect;
	}

	err = chiaki_cond_timedwait_pred(&stream_connection->state_cond, &stream_connection->state_mutex, EXPECT_TIMEOUT_MS, state_finished_cond_check, stream_connection);
	assert(err == CHIAKI_ERR_SUCCESS || err == CHIAKI_ERR_TIMEOUT);
	CHECK_STOP(disconnect);

	if(!stream_connection->state_finished)
	{
		if(err == CHIAKI_ERR_TIMEOUT)
			CHIAKI_LOGE(session->log, "StreamConnection bang receive timeout");

		CHIAKI_LOGE(session->log, "StreamConnection didn't receive bang or failed to handle it");
		err = CHIAKI_ERR_UNKNOWN;
		goto disconnect;
	}

	CHIAKI_LOGI(session->log, "StreamConnection successfully received bang");

	stream_connection->state = STATE_EXPECT_STREAMINFO;
	stream_connection->state_finished = false;
	stream_connection->state_failed = false;
	err = chiaki_cond_timedwait_pred(&stream_connection->state_cond, &stream_connection->state_mutex, EXPECT_TIMEOUT_MS, state_finished_cond_check, stream_connection);
	assert(err == CHIAKI_ERR_SUCCESS || err == CHIAKI_ERR_TIMEOUT);
	CHECK_STOP(disconnect);

	if(!stream_connection->state_finished)
	{
		if(err == CHIAKI_ERR_TIMEOUT)
			CHIAKI_LOGE(session->log, "StreamConnection streaminfo receive timeout");

		CHIAKI_LOGE(session->log, "StreamConnection didn't receive streaminfo");
		err = CHIAKI_ERR_UNKNOWN;
		goto disconnect;
	}

	CHIAKI_LOGI(session->log, "StreamConnection successfully received streaminfo");

	err = chiaki_mutex_lock(&stream_connection->feedback_sender_mutex);
	assert(err == CHIAKI_ERR_SUCCESS);
	err = chiaki_feedback_sender_init(&stream_connection->feedback_sender, &stream_connection->takion);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		chiaki_mutex_unlock(&stream_connection->feedback_sender_mutex);
		CHIAKI_LOGE(stream_connection->log, "StreamConnection failed to start Feedback Sender");
		goto disconnect;
	}
	stream_connection->feedback_sender_active = true;
	chiaki_feedback_sender_set_controller_state(&stream_connection->feedback_sender, &session->controller_state);
	chiaki_mutex_unlock(&stream_connection->feedback_sender_mutex);

	stream_connection->state = STATE_IDLE;
	stream_connection->state_finished = false;
	stream_connection->state_failed = false;

	ChiakiEvent event = { 0 };
	event.type = CHIAKI_EVENT_CONNECTED;
	chiaki_mutex_unlock(&stream_connection->state_mutex);
	chiaki_session_send_event(session, &event);
	err = chiaki_mutex_lock(&stream_connection->state_mutex);
	assert(err == CHIAKI_ERR_SUCCESS);

	while(true)
	{
		err = chiaki_cond_timedwait_pred(&stream_connection->state_cond, &stream_connection->state_mutex, HEARTBEAT_INTERVAL_MS, state_finished_cond_check, stream_connection);
		if(err != CHIAKI_ERR_TIMEOUT)
			break;

		err = stream_connection_send_heartbeat(stream_connection);
		if(err != CHIAKI_ERR_SUCCESS)
			CHIAKI_LOGE(stream_connection->log, "StreamConnection failed to send heartbeat");
		else
			CHIAKI_LOGV(stream_connection->log, "StreamConnection sent heartbeat");
	}

	err = chiaki_mutex_lock(&stream_connection->feedback_sender_mutex);
	assert(err == CHIAKI_ERR_SUCCESS);
	stream_connection->feedback_sender_active = false;
	chiaki_feedback_sender_fini(&stream_connection->feedback_sender);
	chiaki_mutex_unlock(&stream_connection->feedback_sender_mutex);

	err = CHIAKI_ERR_SUCCESS;

disconnect:
	CHIAKI_LOGI(session->log, "StreamConnection is disconnecting");
	stream_connection_send_disconnect(stream_connection);

	if(stream_connection->should_stop)
	{
		CHIAKI_LOGI(stream_connection->log, "StreamConnection was requested to stop");
		err = CHIAKI_ERR_CANCELED;
	}
	else if(stream_connection->remote_disconnected)
	{
		CHIAKI_LOGI(stream_connection->log, "StreamConnection closing after Remote disconnected");
		err = CHIAKI_ERR_DISCONNECTED;
	}

err_congestion_control:
	chiaki_congestion_control_stop(&congestion_control);

close_takion:
	chiaki_mutex_unlock(&stream_connection->state_mutex);

	chiaki_takion_close(&stream_connection->takion);
	CHIAKI_LOGI(session->log, "StreamConnection closed takion");

err_video_receiver:
	chiaki_video_receiver_free(stream_connection->video_receiver);
	stream_connection->video_receiver = NULL;

err_audio_receiver:
	chiaki_audio_receiver_free(stream_connection->audio_receiver);
	stream_connection->audio_receiver = NULL;

	return err;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_stream_connection_stop(ChiakiStreamConnection *stream_connection)
{
	ChiakiErrorCode err = chiaki_mutex_lock(&stream_connection->state_mutex);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	stream_connection->should_stop = true;
	ChiakiErrorCode unlock_err = chiaki_mutex_unlock(&stream_connection->state_mutex);
	err = chiaki_cond_signal(&stream_connection->state_cond);
	return err == CHIAKI_ERR_SUCCESS ? unlock_err : err;
}

static void stream_connection_takion_cb(ChiakiTakionEvent *event, void *user)
{
	ChiakiStreamConnection *stream_connection = user;
	switch(event->type)
	{
		case CHIAKI_TAKION_EVENT_TYPE_CONNECTED:
		case CHIAKI_TAKION_EVENT_TYPE_DISCONNECT:
			chiaki_mutex_lock(&stream_connection->state_mutex);
			if(stream_connection->state == STATE_TAKION_CONNECT)
			{
				stream_connection->state_finished = event->type == CHIAKI_TAKION_EVENT_TYPE_CONNECTED;
				stream_connection->state_failed = event->type == CHIAKI_TAKION_EVENT_TYPE_DISCONNECT;
				chiaki_cond_signal(&stream_connection->state_cond);
			}
			chiaki_mutex_unlock(&stream_connection->state_mutex);
			break;
		case CHIAKI_TAKION_EVENT_TYPE_DATA:
			stream_connection_takion_data(stream_connection, event->data.data_type, event->data.buf, event->data.buf_size);
			break;
		case CHIAKI_TAKION_EVENT_TYPE_AV:
			stream_connection_takion_av(stream_connection, event->av);
			break;
		default:
			break;
	}
}

static void stream_connection_takion_data(ChiakiStreamConnection *stream_connection, ChiakiTakionMessageDataType data_type, uint8_t *buf, size_t buf_size)
{
	switch(data_type)
	{
		case CHIAKI_TAKION_MESSAGE_DATA_TYPE_PROTOBUF:
			stream_connection_takion_data_protobuf(stream_connection, buf, buf_size);
			break;
		case CHIAKI_TAKION_MESSAGE_DATA_TYPE_RUMBLE:
			stream_connection_takion_data_rumble(stream_connection, buf, buf_size);
			break;
		default:
			break;
	}
}

static void stream_connection_takion_data_protobuf(ChiakiStreamConnection *stream_connection, uint8_t *buf, size_t buf_size)
{
	chiaki_mutex_lock(&stream_connection->state_mutex);
	switch(stream_connection->state)
	{
		case STATE_EXPECT_BANG:
			stream_connection_takion_data_expect_bang(stream_connection, buf, buf_size);
			break;
		case STATE_EXPECT_STREAMINFO:
			stream_connection_takion_data_expect_streaminfo(stream_connection, buf, buf_size);
			break;
		default: // STATE_IDLE
			stream_connection_takion_data_idle(stream_connection, buf, buf_size);
			break;
	}
	chiaki_mutex_unlock(&stream_connection->state_mutex);
}

static void stream_connection_takion_data_rumble(ChiakiStreamConnection *stream_connection, uint8_t *buf, size_t buf_size)
{
	if(buf_size < 3)
	{
		CHIAKI_LOGE(stream_connection->log, "StreamConnection got rumble packet with size %#llx < 3",
				(unsigned long long)buf_size);
		return;
	}
	ChiakiEvent event = { 0 };
	event.type = CHIAKI_EVENT_RUMBLE;
	event.rumble.unknown = buf[0];
	event.rumble.left = buf[1];
	event.rumble.right = buf[2];
	chiaki_session_send_event(stream_connection->session, &event);
}

static void stream_connection_takion_data_handle_disconnect(ChiakiStreamConnection *stream_connection, uint8_t *buf, size_t buf_size)
{
	tkproto_TakionMessage msg;
	memset(&msg, 0, sizeof(msg));

	char reason[256];
	ChiakiPBDecodeBuf decode_buf;
	decode_buf.size = 0;
	decode_buf.max_size = sizeof(reason) - 1;
	decode_buf.buf = (uint8_t *)reason;
	msg.disconnect_payload.reason.arg = &decode_buf;
	msg.disconnect_payload.reason.funcs.decode = chiaki_pb_decode_buf;

	pb_istream_t stream = pb_istream_from_buffer(buf, buf_size);
	bool r = pb_decode(&stream, tkproto_TakionMessage_fields, &msg);
	if(!r)
	{
		CHIAKI_LOGE(stream_connection->log, "StreamConnection failed to decode data protobuf");
		chiaki_log_hexdump(stream_connection->log, CHIAKI_LOG_ERROR, buf, buf_size);
		return;
	}

	reason[decode_buf.size] = '\0';
	CHIAKI_LOGI(stream_connection->log, "Remote disconnected from StreamConnection with reason \"%s\"", reason);

	stream_connection->remote_disconnected = true;
	free(stream_connection->remote_disconnect_reason);
	stream_connection->remote_disconnect_reason = strdup(reason);
	chiaki_cond_signal(&stream_connection->state_cond);
}

static void stream_connection_takion_data_idle(ChiakiStreamConnection *stream_connection, uint8_t *buf, size_t buf_size)
{
	tkproto_TakionMessage msg;
	memset(&msg, 0, sizeof(msg));

	pb_istream_t stream = pb_istream_from_buffer(buf, buf_size);
	bool r = pb_decode(&stream, tkproto_TakionMessage_fields, &msg);
	if(!r)
	{
		CHIAKI_LOGE(stream_connection->log, "StreamConnection failed to decode data protobuf");
		chiaki_log_hexdump(stream_connection->log, CHIAKI_LOG_ERROR, buf, buf_size);
		return;
	}

	CHIAKI_LOGV(stream_connection->log, "StreamConnection received data");
	chiaki_log_hexdump(stream_connection->log, CHIAKI_LOG_VERBOSE, buf, buf_size);

	if(msg.type == tkproto_TakionMessage_PayloadType_DISCONNECT)
		stream_connection_takion_data_handle_disconnect(stream_connection, buf, buf_size);
}

static ChiakiErrorCode stream_connection_init_crypt(ChiakiStreamConnection *stream_connection)
{
	ChiakiSession *session = stream_connection->session;

	stream_connection->gkcrypt_local = chiaki_gkcrypt_new(stream_connection->log, CHIAKI_GKCRYPT_KEY_BUF_BLOCKS_DEFAULT, 2, session->handshake_key, stream_connection->ecdh_secret);
	if(!stream_connection->gkcrypt_local)
	{
		CHIAKI_LOGE(stream_connection->log, "StreamConnection failed to initialize local GKCrypt with index 2");
		return CHIAKI_ERR_UNKNOWN;
	}
	stream_connection->gkcrypt_remote = chiaki_gkcrypt_new(stream_connection->log, CHIAKI_GKCRYPT_KEY_BUF_BLOCKS_DEFAULT, 3, session->handshake_key, stream_connection->ecdh_secret);
	if(!stream_connection->gkcrypt_remote)
	{
		CHIAKI_LOGE(stream_connection->log, "StreamConnection failed to initialize remote GKCrypt with index 3");
		free(stream_connection->gkcrypt_local);
		stream_connection->gkcrypt_local = NULL;
		return CHIAKI_ERR_UNKNOWN;
	}

	chiaki_takion_set_crypt(&stream_connection->takion, stream_connection->gkcrypt_local, stream_connection->gkcrypt_remote);

	return CHIAKI_ERR_SUCCESS;
}

static void stream_connection_takion_data_expect_bang(ChiakiStreamConnection *stream_connection, uint8_t *buf, size_t buf_size)
{
	char ecdh_pub_key[128];
	ChiakiPBDecodeBuf ecdh_pub_key_buf = { sizeof(ecdh_pub_key), 0, (uint8_t *)ecdh_pub_key };
	char ecdh_sig[32];
	ChiakiPBDecodeBuf ecdh_sig_buf = { sizeof(ecdh_sig), 0, (uint8_t *)ecdh_sig };

	tkproto_TakionMessage msg;
	memset(&msg, 0, sizeof(msg));

	msg.bang_payload.ecdh_pub_key.arg = &ecdh_pub_key_buf;
	msg.bang_payload.ecdh_pub_key.funcs.decode = chiaki_pb_decode_buf;
	msg.bang_payload.ecdh_sig.arg = &ecdh_sig_buf;
	msg.bang_payload.ecdh_sig.funcs.decode = chiaki_pb_decode_buf;

	pb_istream_t stream = pb_istream_from_buffer(buf, buf_size);
	bool r = pb_decode(&stream, tkproto_TakionMessage_fields, &msg);
	if(!r)
	{
		CHIAKI_LOGE(stream_connection->log, "StreamConnection failed to decode data protobuf");
		return;
	}

	if(msg.type != tkproto_TakionMessage_PayloadType_BANG || !msg.has_bang_payload)
	{
		if(msg.type == tkproto_TakionMessage_PayloadType_DISCONNECT)
		{
			stream_connection_takion_data_handle_disconnect(stream_connection, buf, buf_size);
			return;
		}

		CHIAKI_LOGE(stream_connection->log, "StreamConnection expected bang payload but received something else");
		return;
	}

	CHIAKI_LOGI(stream_connection->log, "BANG received");

	if(!msg.bang_payload.version_accepted)
	{
		CHIAKI_LOGE(stream_connection->log, "StreamConnection bang remote didn't accept version");
		goto error;
	}

	if(!msg.bang_payload.encrypted_key_accepted)
	{
		CHIAKI_LOGE(stream_connection->log, "StreamConnection bang remote didn't accept encrypted key");
		goto error;
	}

	if(!ecdh_pub_key_buf.size)
	{
		CHIAKI_LOGE(stream_connection->log, "StreamConnection didn't get remote ECDH pub key from bang");
		goto error;
	}

	if(!ecdh_sig_buf.size)
	{
		CHIAKI_LOGE(stream_connection->log, "StreamConnection didn't get remote ECDH sig from bang");
		goto error;
	}

	assert(!stream_connection->ecdh_secret);
	stream_connection->ecdh_secret = malloc(CHIAKI_ECDH_SECRET_SIZE);
	if(!stream_connection->ecdh_secret)
	{
		CHIAKI_LOGE(stream_connection->log, "StreamConnection failed to alloc ECDH secret memory");
		goto error;
	}

	ChiakiErrorCode err = chiaki_ecdh_derive_secret(&stream_connection->session->ecdh,
			stream_connection->ecdh_secret,
			ecdh_pub_key_buf.buf, ecdh_pub_key_buf.size,
			stream_connection->session->handshake_key,
			ecdh_sig_buf.buf, ecdh_sig_buf.size);

	if(err != CHIAKI_ERR_SUCCESS)
	{
		free(stream_connection->ecdh_secret);
		stream_connection->ecdh_secret = NULL;
		CHIAKI_LOGE(stream_connection->log, "StreamConnection failed to derive secret from bang");
		goto error;
	}

	err = stream_connection_init_crypt(stream_connection);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(stream_connection->log, "StreamConnection failed to init crypt after receiving bang");
		goto error;
	}

	// stream_connection->state_mutex is expected to be locked by the caller of this function
	stream_connection->state_finished = true;
	chiaki_cond_signal(&stream_connection->state_cond);
	return;
error:
	stream_connection->state_failed = true;
	chiaki_cond_signal(&stream_connection->state_cond);
}

typedef struct decode_resolutions_context_t
{
	ChiakiStreamConnection *stream_connection;
	ChiakiVideoProfile video_profiles[CHIAKI_VIDEO_PROFILES_MAX];
	size_t video_profiles_count;
} DecodeResolutionsContext;

static bool pb_decode_resolution(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
	DecodeResolutionsContext *ctx = *arg;

	tkproto_ResolutionPayload resolution = { 0 };
	ChiakiPBDecodeBufAlloc header_buf = { 0 };
	resolution.video_header.arg = &header_buf;
	resolution.video_header.funcs.decode = chiaki_pb_decode_buf_alloc;
	if(!pb_decode(stream, tkproto_ResolutionPayload_fields, &resolution))
		return false;

	if(!header_buf.buf)
	{
		CHIAKI_LOGE(ctx->stream_connection->session->log, "Failed to decode video header");
		return true;
	}

	uint8_t *header_buf_padded = realloc(header_buf.buf, header_buf.size + CHIAKI_VIDEO_BUFFER_PADDING_SIZE);
	if(!header_buf_padded)
	{
		free(header_buf.buf);
		CHIAKI_LOGE(ctx->stream_connection->session->log, "Failed to realloc video header with padding");
		return true;
	}
	memset(header_buf_padded + header_buf.size, 0, CHIAKI_VIDEO_BUFFER_PADDING_SIZE);

	if(ctx->video_profiles_count >= CHIAKI_VIDEO_PROFILES_MAX)
	{
		CHIAKI_LOGE(ctx->stream_connection->session->log, "Received more resolutions than the maximum");
		return true;
	}

	ChiakiVideoProfile *profile = &ctx->video_profiles[ctx->video_profiles_count++];
	profile->width = resolution.width;
	profile->height = resolution.height;
	profile->header_sz = header_buf.size;
	profile->header = header_buf_padded;
	return true;
}

static void stream_connection_takion_data_expect_streaminfo(ChiakiStreamConnection *stream_connection, uint8_t *buf, size_t buf_size)
{
	tkproto_TakionMessage msg;
	memset(&msg, 0, sizeof(msg));

	uint8_t audio_header[CHIAKI_AUDIO_HEADER_SIZE];
	ChiakiPBDecodeBuf audio_header_buf = { sizeof(audio_header), 0, (uint8_t *)audio_header };
	msg.stream_info_payload.audio_header.arg = &audio_header_buf;
	msg.stream_info_payload.audio_header.funcs.decode = chiaki_pb_decode_buf;

	DecodeResolutionsContext decode_resolutions_context;
	decode_resolutions_context.stream_connection = stream_connection;
	memset(decode_resolutions_context.video_profiles, 0, sizeof(decode_resolutions_context.video_profiles));
	decode_resolutions_context.video_profiles_count = 0;
	msg.stream_info_payload.resolution.arg = &decode_resolutions_context;
	msg.stream_info_payload.resolution.funcs.decode = pb_decode_resolution;

	pb_istream_t stream = pb_istream_from_buffer(buf, buf_size);
	bool r = pb_decode(&stream, tkproto_TakionMessage_fields, &msg);
	if(!r)
	{
		CHIAKI_LOGE(stream_connection->log, "StreamConnection failed to decode data protobuf");
		return;
	}

	if(msg.type != tkproto_TakionMessage_PayloadType_STREAMINFO || !msg.has_stream_info_payload)
	{
		if(msg.type == tkproto_TakionMessage_PayloadType_DISCONNECT)
		{
			stream_connection_takion_data_handle_disconnect(stream_connection, buf, buf_size);
			return;
		}

		CHIAKI_LOGE(stream_connection->log, "StreamConnection expected streaminfo payload but received something else");
		chiaki_log_hexdump(stream_connection->log, CHIAKI_LOG_VERBOSE, buf, buf_size);
		return;
	}

	CHIAKI_LOGD(stream_connection->log, "StreamConnection received audio header:");
	chiaki_log_hexdump(stream_connection->log, CHIAKI_LOG_DEBUG, audio_header, audio_header_buf.size);

	if(audio_header_buf.size != CHIAKI_AUDIO_HEADER_SIZE)
	{
		CHIAKI_LOGE(stream_connection->log, "StreamConnection received invalid audio header in streaminfo");
		goto error;
	}

	ChiakiAudioHeader audio_header_s;
	chiaki_audio_header_load(&audio_header_s, audio_header);
	chiaki_audio_receiver_stream_info(stream_connection->audio_receiver, &audio_header_s);

	chiaki_video_receiver_stream_info(stream_connection->video_receiver,
			decode_resolutions_context.video_profiles,
			decode_resolutions_context.video_profiles_count);

	// TODO: do some checks?

	stream_connection_send_streaminfo_ack(stream_connection);

	// stream_connection->state_mutex is expected to be locked by the caller of this function
	stream_connection->state_finished = true;
	chiaki_cond_signal(&stream_connection->state_cond);
	return;
error:
	stream_connection->state_failed = true;
	chiaki_cond_signal(&stream_connection->state_cond);
}

static bool chiaki_pb_encode_zero_encrypted_key(pb_ostream_t *stream, const pb_field_t *field, void *const *arg)
{
	if(!pb_encode_tag_for_field(stream, field))
		return false;
	uint8_t data[] = { 0, 0, 0, 0 };
	return pb_encode_string(stream, data, sizeof(data));
}

#define LAUNCH_SPEC_JSON_BUF_SIZE 1024

static ChiakiErrorCode stream_connection_send_big(ChiakiStreamConnection *stream_connection)
{
	ChiakiSession *session = stream_connection->session;

	ChiakiLaunchSpec launch_spec;
	launch_spec.target = session->target;
	launch_spec.mtu = session->mtu_in;
	launch_spec.rtt = session->rtt_us / 1000;
	launch_spec.handshake_key = session->handshake_key;

	launch_spec.width = session->connect_info.video_profile.width;
	launch_spec.height = session->connect_info.video_profile.height;
	launch_spec.max_fps = session->connect_info.video_profile.max_fps;
	launch_spec.codec = session->connect_info.video_profile.codec;
	launch_spec.bw_kbps_sent = session->connect_info.video_profile.bitrate;

	union
	{
		char json[LAUNCH_SPEC_JSON_BUF_SIZE];
		char b64[LAUNCH_SPEC_JSON_BUF_SIZE * 2];
	} launch_spec_buf;
	int launch_spec_json_size = chiaki_launchspec_format(launch_spec_buf.json, sizeof(launch_spec_buf.json), &launch_spec);
	if(launch_spec_json_size < 0)
	{
		CHIAKI_LOGE(stream_connection->log, "StreamConnection failed to format LaunchSpec json");
		return CHIAKI_ERR_UNKNOWN;
	}
	launch_spec_json_size += 1; // we also want the trailing 0

	CHIAKI_LOGV(stream_connection->log, "LaunchSpec: %s", launch_spec_buf.json);

	uint8_t launch_spec_json_enc[LAUNCH_SPEC_JSON_BUF_SIZE];
	memset(launch_spec_json_enc, 0, (size_t)launch_spec_json_size);
	ChiakiErrorCode err = chiaki_rpcrypt_encrypt(&session->rpcrypt, 0, launch_spec_json_enc, launch_spec_json_enc,
			(size_t)launch_spec_json_size);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(stream_connection->log, "StreamConnection failed to encrypt LaunchSpec");
		return err;
	}

	xor_bytes(launch_spec_json_enc, (uint8_t *)launch_spec_buf.json, (size_t)launch_spec_json_size);
	err = chiaki_base64_encode(launch_spec_json_enc, (size_t)launch_spec_json_size, launch_spec_buf.b64, sizeof(launch_spec_buf.b64));
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(stream_connection->log, "StreamConnection failed to encode LaunchSpec as base64");
		return err;
	}

	uint8_t ecdh_pub_key[128];
	ChiakiPBBuf ecdh_pub_key_buf = { sizeof(ecdh_pub_key), ecdh_pub_key };
	uint8_t ecdh_sig[32];
	ChiakiPBBuf ecdh_sig_buf = { sizeof(ecdh_sig), ecdh_sig };
	err = chiaki_ecdh_get_local_pub_key(&session->ecdh,
			ecdh_pub_key, &ecdh_pub_key_buf.size,
			session->handshake_key,
			ecdh_sig, &ecdh_sig_buf.size);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(stream_connection->log, "StreamConnection failed to get ECDH key and sig");
		return err;
	}

	tkproto_TakionMessage msg;
	memset(&msg, 0, sizeof(msg));

	msg.type = tkproto_TakionMessage_PayloadType_BIG;
	msg.has_big_payload = true;
	msg.big_payload.client_version = stream_connection->takion.version;
	msg.big_payload.session_key.arg = session->session_id;
	msg.big_payload.session_key.funcs.encode = chiaki_pb_encode_string;
	msg.big_payload.launch_spec.arg = launch_spec_buf.b64;
	msg.big_payload.launch_spec.funcs.encode = chiaki_pb_encode_string;
	msg.big_payload.encrypted_key.funcs.encode = chiaki_pb_encode_zero_encrypted_key;
	msg.big_payload.ecdh_pub_key.arg = &ecdh_pub_key_buf;
	msg.big_payload.ecdh_pub_key.funcs.encode = chiaki_pb_encode_buf;
	msg.big_payload.ecdh_sig.arg = &ecdh_sig_buf;
	msg.big_payload.ecdh_sig.funcs.encode = chiaki_pb_encode_buf;

	uint8_t buf[2048];
	size_t buf_size;

	pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
	bool pbr = pb_encode(&stream, tkproto_TakionMessage_fields, &msg);
	if(!pbr)
	{
		CHIAKI_LOGE(stream_connection->log, "StreamConnection big protobuf encoding failed");
		return CHIAKI_ERR_UNKNOWN;
	}

	buf_size = stream.bytes_written;
	err = chiaki_takion_send_message_data(&stream_connection->takion, 1, 1, buf, buf_size, NULL);

	return err;
}

static ChiakiErrorCode stream_connection_send_streaminfo_ack(ChiakiStreamConnection *stream_connection)
{
	tkproto_TakionMessage msg;
	memset(&msg, 0, sizeof(msg));
	msg.type = tkproto_TakionMessage_PayloadType_STREAMINFOACK;

	uint8_t buf[3];
	size_t buf_size;

	pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
	bool pbr = pb_encode(&stream, tkproto_TakionMessage_fields, &msg);
	if(!pbr)
	{
		CHIAKI_LOGE(stream_connection->log, "StreamConnection streaminfo ack protobuf encoding failed");
		return CHIAKI_ERR_UNKNOWN;
	}

	buf_size = stream.bytes_written;
	return chiaki_takion_send_message_data(&stream_connection->takion, 1, 9, buf, buf_size, NULL);
}

static ChiakiErrorCode stream_connection_send_disconnect(ChiakiStreamConnection *stream_connection)
{
	tkproto_TakionMessage msg;
	memset(&msg, 0, sizeof(msg));

	msg.type = tkproto_TakionMessage_PayloadType_DISCONNECT;
	msg.has_disconnect_payload = true;
	msg.disconnect_payload.reason.arg = "Client Disconnecting";
	msg.disconnect_payload.reason.funcs.encode = chiaki_pb_encode_string;

	uint8_t buf[26];
	size_t buf_size;

	pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
	bool pbr = pb_encode(&stream, tkproto_TakionMessage_fields, &msg);
	if(!pbr)
	{
		CHIAKI_LOGE(stream_connection->log, "StreamConnection disconnect protobuf encoding failed");
		return CHIAKI_ERR_UNKNOWN;
	}

	CHIAKI_LOGI(stream_connection->log, "StreamConnection sending Disconnect");

	buf_size = stream.bytes_written;
	ChiakiErrorCode err = chiaki_takion_send_message_data(&stream_connection->takion, 1, 1, buf, buf_size, NULL);

	return err;
}

static void stream_connection_takion_av(ChiakiStreamConnection *stream_connection, ChiakiTakionAVPacket *packet)
{
	chiaki_gkcrypt_decrypt(stream_connection->gkcrypt_remote, packet->key_pos + CHIAKI_GKCRYPT_BLOCK_SIZE, packet->data, packet->data_size);

	if(packet->is_video)
		chiaki_video_receiver_av_packet(stream_connection->video_receiver, packet);
	else
		chiaki_audio_receiver_av_packet(stream_connection->audio_receiver, packet);
}

static ChiakiErrorCode stream_connection_send_heartbeat(ChiakiStreamConnection *stream_connection)
{
	tkproto_TakionMessage msg = { 0 };
	msg.type = tkproto_TakionMessage_PayloadType_HEARTBEAT;

	uint8_t buf[8];

	pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
	bool pbr = pb_encode(&stream, tkproto_TakionMessage_fields, &msg);
	if(!pbr)
	{
		CHIAKI_LOGE(stream_connection->log, "StreamConnection heartbeat protobuf encoding failed");
		return CHIAKI_ERR_UNKNOWN;
	}

	return chiaki_takion_send_message_data(&stream_connection->takion, 1, 1, buf, stream.bytes_written, NULL);
}

CHIAKI_EXPORT ChiakiErrorCode stream_connection_send_corrupt_frame(ChiakiStreamConnection *stream_connection, ChiakiSeqNum16 start, ChiakiSeqNum16 end)
{
	tkproto_TakionMessage msg = { 0 };
	msg.type = tkproto_TakionMessage_PayloadType_CORRUPTFRAME;
	msg.has_corrupt_payload = true;
	msg.corrupt_payload.start = start;
	msg.corrupt_payload.end = end;

	uint8_t buf[0x10];

	pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
	bool pbr = pb_encode(&stream, tkproto_TakionMessage_fields, &msg);
	if(!pbr)
	{
		CHIAKI_LOGE(stream_connection->log, "StreamConnection heartbeat protobuf encoding failed");
		return CHIAKI_ERR_UNKNOWN;
	}

	CHIAKI_LOGD(stream_connection->log, "StreamConnection reporting corrupt frame(s) from %u to %u", (unsigned int)start, (unsigned int)end);
	return chiaki_takion_send_message_data(&stream_connection->takion, 1, 2, buf, stream.bytes_written, NULL);
}
